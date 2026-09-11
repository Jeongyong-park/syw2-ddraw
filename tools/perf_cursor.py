"""Opt-in, foreground-only cursor movement for Windows capture experiments. No clicks."""
import ctypes as c
from ctypes import wintypes as w


def sweep_point(seconds, rect):
    left,top,right,bottom=rect
    if right-left<10 or bottom-top<10: raise ValueError('Client too small for cursor sweep')
    phase=seconds%4
    fraction=phase if phase<1 else 1 if phase<2 else 3-phase if phase<3 else 0
    return (round(left+(right-left-1)*(.2+.6*fraction)), round(top+(bottom-top-1)*.4))


def absolute_coordinate(value, origin, extent):
    if extent<=0 or not origin<=value<origin+extent: raise ValueError('Cursor point outside virtual desktop')
    return min(65535,((value-origin)*65536+32768)//extent)


class CursorSweep:
    def __init__(self, user32, hwnd, pid, qpc):
        self.u=user32; self.hwnd=hwnd; self.pid=pid; self.qpc=qpc; self.events=[]; self.last=None; self.sequence=0
        class Mouse(c.Structure):
            _fields_=[('dx',w.LONG),('dy',w.LONG),('mouseData',w.DWORD),('flags',w.DWORD),('time',w.DWORD),('extra',c.c_size_t)]
        class Payload(c.Union): _fields_=[('mouse',Mouse)]
        class Input(c.Structure): _fields_=[('type',w.DWORD),('payload',Payload)]
        self.Input=Input
        self.u.SendInput.argtypes=[w.UINT,c.POINTER(Input),c.c_int]; self.u.SendInput.restype=w.UINT
        self.u.ClientToScreen.argtypes=[w.HWND,c.POINTER(w.POINT)]
        self.u.GetCursorPos.argtypes=[c.POINTER(w.POINT)]
        self.u.GetAsyncKeyState.argtypes=[c.c_int]; self.u.GetAsyncKeyState.restype=c.c_short
        self.rect=self.client_rect(); self.original=self.position()
        self.desktop=tuple(self.u.GetSystemMetrics(i) for i in (76,77,78,79))

    def client_rect(self):
        rect=w.RECT(); origin=w.POINT()
        if not self.u.GetClientRect(self.hwnd,c.byref(rect)) or not self.u.ClientToScreen(self.hwnd,c.byref(origin)):
            raise c.WinError(c.get_last_error())
        return (origin.x,origin.y,origin.x+rect.right,origin.y+rect.bottom)

    def position(self):
        point=w.POINT()
        if not self.u.GetCursorPos(c.byref(point)): raise c.WinError(c.get_last_error())
        return (point.x,point.y)

    def foreground(self):
        owner=w.DWORD(); self.u.GetWindowThreadProcessId(self.u.GetForegroundWindow(),c.byref(owner))
        return owner.value==self.pid

    def buttons_down(self):
        return any(self.u.GetAsyncKeyState(key)&0x8000 for key in (1,2,4,5,6))

    def move(self, point, tagged=True):
        x,y,width,height=self.desktop
        event=self.Input(); event.payload.mouse.dx=absolute_coordinate(point[0],x,width)
        event.payload.mouse.dy=absolute_coordinate(point[1],y,height)
        event.payload.mouse.flags=0xC001 # MOVE | ABSOLUTE | VIRTUALDESK; normal coalescing, no buttons
        if tagged:
            if self.sequence>=65535: raise RuntimeError('Cursor sweep sequence exhausted')
            self.sequence+=1
            event.payload.mouse.extra=0x48510000 | self.sequence
        before=self.qpc()
        if self.u.SendInput(1,c.byref(event),c.sizeof(event))!=1: raise RuntimeError('SendInput did not accept mouse movement')
        after=self.qpc(); self.last=point
        return (before,after,point[0],point[1],self.sequence if tagged else 0)

    def step(self, seconds):
        if not self.foreground(): raise RuntimeError('Cursor sweep stopped: game lost foreground')
        if self.buttons_down(): raise RuntimeError('Cursor sweep stopped: mouse button held')
        if self.client_rect()!=self.rect: raise RuntimeError('Cursor sweep stopped: client geometry changed')
        actual=self.position()
        if self.last and max(abs(a-b) for a,b in zip(actual,self.last))>3:
            raise RuntimeError(f'Cursor sweep stopped: cursor moved outside scripted path (expected {self.last}, actual {actual})')
        point=sweep_point(seconds,self.rect)
        if point!=self.last: self.events.append(self.move(point))

    def restore(self):
        # Do not override a user's changed focus or cursor position.
        if self.last and self.foreground() and not self.buttons_down() and max(abs(a-b) for a,b in zip(self.position(),self.last))<=3:
            self.move(self.original,tagged=False)
