#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <wchar.h>

#define B2L_MT_MAX_CONTACTS 16U
#define B2L_MT_CLOSE_HEIGHT  100
#define B2L_MT_CLOSE_WIDTH   260
#define B2L_MT_RADIUS        52

typedef enum _B2L_MT_INPUT_MODE {
    B2lMtInputNone = 0,
    B2lMtInputPointer,
    B2lMtInputTouch,
    B2lMtInputMouse
} B2L_MT_INPUT_MODE;

typedef struct _B2L_MT_CONTACT {
    BOOL Active;
    UINT32 Id;
    POINT Position;
} B2L_MT_CONTACT;

static const WCHAR B2lMtClassName[] = L"Beyond2LTEMultiTouchTest";
static B2L_MT_CONTACT B2lMtContacts[B2L_MT_MAX_CONTACTS];
static B2L_MT_INPUT_MODE B2lMtInputMode;

static const COLORREF B2lMtColors[] = {
    RGB(0, 255, 80), RGB(0, 190, 255), RGB(255, 210, 0), RGB(255, 70, 120),
    RGB(180, 90, 255), RGB(0, 255, 220), RGB(255, 130, 0), RGB(150, 255, 0),
    RGB(255, 0, 230), RGB(120, 160, 255), RGB(255, 255, 255), RGB(80, 220, 160),
    RGB(220, 120, 80), RGB(170, 170, 255), RGB(255, 170, 210), RGB(170, 255, 210)
};

static VOID
B2lMtClearContacts(VOID)
{
    ZeroMemory(B2lMtContacts, sizeof(B2lMtContacts));
}

static B2L_MT_CONTACT *
B2lMtFindContact(UINT32 Id, BOOL Create)
{
    B2L_MT_CONTACT *FreeContact = NULL;
    UINT Index;

    for (Index = 0U; Index < B2L_MT_MAX_CONTACTS; ++Index) {
        if (B2lMtContacts[Index].Active && (B2lMtContacts[Index].Id == Id)) {
            return &B2lMtContacts[Index];
        }
        if (!B2lMtContacts[Index].Active && (FreeContact == NULL)) {
            FreeContact = &B2lMtContacts[Index];
        }
    }
    if (Create && (FreeContact != NULL)) {
        FreeContact->Active = TRUE;
        FreeContact->Id = Id;
        FreeContact->Position.x = 0;
        FreeContact->Position.y = 0;
        return FreeContact;
    }
    return NULL;
}

static VOID
B2lMtUpdateContact(
    HWND Window,
    UINT32 Id,
    POINT ScreenPosition,
    BOOL Active
    )
{
    B2L_MT_CONTACT *Contact;

    Contact = B2lMtFindContact(Id, Active);
    if (Contact == NULL) {
        return;
    }
    (VOID)ScreenToClient(Window, &ScreenPosition);
    Contact->Position = ScreenPosition;
    if (!Active) {
        Contact->Active = FALSE;
    }
    (VOID)InvalidateRect(Window, NULL, FALSE);
}

static UINT
B2lMtActiveCount(VOID)
{
    UINT Count = 0U;
    UINT Index;

    for (Index = 0U; Index < B2L_MT_MAX_CONTACTS; ++Index) {
        if (B2lMtContacts[Index].Active) {
            ++Count;
        }
    }
    return Count;
}

static LPCWSTR
B2lMtModeText(VOID)
{
    switch (B2lMtInputMode) {
    case B2lMtInputPointer:
        return L"WM_POINTER (gercek Windows touch)";
    case B2lMtInputTouch:
        return L"WM_TOUCH (gercek Windows touch)";
    case B2lMtInputMouse:
        return L"MOUSE ONLY - multi-touch olayi gelmedi";
    default:
        return L"Touch olayi bekleniyor";
    }
}

static BOOL
B2lMtCloseHit(HWND Window, POINT ClientPosition)
{
    RECT Client;

    (VOID)GetClientRect(Window, &Client);
    return (ClientPosition.y >= 0) &&
           (ClientPosition.y < B2L_MT_CLOSE_HEIGHT) &&
           (ClientPosition.x >= (Client.right - B2L_MT_CLOSE_WIDTH));
}

static VOID
B2lMtPaint(HWND Window)
{
    WCHAR Text[160];
    PAINTSTRUCT Paint;
    RECT Client;
    RECT CloseRect;
    RECT TextRect;
    HBRUSH Background;
    HBRUSH Brush;
    HFONT HeaderFont;
    HFONT DetailFont;
    HGDIOBJ OldFont;
    HGDIOBJ OldBrush;
    HDC Dc;
    UINT ActiveCount;
    UINT Index;
    INT ListY;

    Dc = BeginPaint(Window, &Paint);
    if (Dc == NULL) {
        return;
    }
    (VOID)GetClientRect(Window, &Client);
    Background = CreateSolidBrush(RGB(12, 12, 16));
    if (Background != NULL) {
        (VOID)FillRect(Dc, &Client, Background);
    }
    (VOID)SetBkMode(Dc, TRANSPARENT);
    (VOID)SetTextColor(Dc, RGB(255, 255, 255));

    HeaderFont = CreateFontW(
        52, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    DetailFont = CreateFontW(
        30, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Consolas"
        );
    OldFont = SelectObject(
        Dc,
        (HeaderFont != NULL) ? (HGDIOBJ)HeaderFont : GetStockObject(DEFAULT_GUI_FONT)
        );
    ActiveCount = B2lMtActiveCount();
    (VOID)swprintf_s(
        Text,
        ARRAYSIZE(Text),
        L"AKTIF PARMAK: %u / 10",
        ActiveCount
        );
    TextRect = Client;
    TextRect.left = 24;
    TextRect.top = 12;
    TextRect.right -= B2L_MT_CLOSE_WIDTH + 12;
    TextRect.bottom = 78;
    (VOID)DrawTextW(Dc, Text, -1, &TextRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    CloseRect = Client;
    CloseRect.left = Client.right - B2L_MT_CLOSE_WIDTH;
    CloseRect.bottom = B2L_MT_CLOSE_HEIGHT;
    Brush = CreateSolidBrush(RGB(150, 25, 35));
    if (Brush != NULL) {
        (VOID)FillRect(Dc, &CloseRect, Brush);
        (VOID)DeleteObject(Brush);
    }
    (VOID)DrawTextW(Dc, L"KAPAT", -1, &CloseRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    (VOID)SelectObject(
        Dc,
        (DetailFont != NULL) ? (HGDIOBJ)DetailFont : GetStockObject(DEFAULT_GUI_FONT)
        );
    TextRect.left = 24;
    TextRect.top = 82;
    TextRect.right = Client.right - 24;
    TextRect.bottom = 126;
    (VOID)DrawTextW(Dc, B2lMtModeText(), -1, &TextRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ListY = 138;
    for (Index = 0U; Index < B2L_MT_MAX_CONTACTS; ++Index) {
        RECT Circle;
        RECT Label;

        if (!B2lMtContacts[Index].Active) {
            continue;
        }
        Brush = CreateSolidBrush(B2lMtColors[Index]);
        OldBrush = SelectObject(
            Dc,
            (Brush != NULL) ? (HGDIOBJ)Brush : GetStockObject(WHITE_BRUSH)
            );
        Circle.left = B2lMtContacts[Index].Position.x - B2L_MT_RADIUS;
        Circle.top = B2lMtContacts[Index].Position.y - B2L_MT_RADIUS;
        Circle.right = B2lMtContacts[Index].Position.x + B2L_MT_RADIUS;
        Circle.bottom = B2lMtContacts[Index].Position.y + B2L_MT_RADIUS;
        (VOID)Ellipse(Dc, Circle.left, Circle.top, Circle.right, Circle.bottom);
        (VOID)SelectObject(Dc, OldBrush);
        if (Brush != NULL) {
            (VOID)DeleteObject(Brush);
        }

        (VOID)swprintf_s(
            Text,
            ARRAYSIZE(Text),
            L"ID=%lu  X=%ld  Y=%ld",
            (unsigned long)B2lMtContacts[Index].Id,
            B2lMtContacts[Index].Position.x,
            B2lMtContacts[Index].Position.y
            );
        TextRect.left = 24;
        TextRect.top = ListY;
        TextRect.right = Client.right - 24;
        TextRect.bottom = ListY + 38;
        (VOID)SetTextColor(Dc, B2lMtColors[Index]);
        (VOID)DrawTextW(Dc, Text, -1, &TextRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        ListY += 40;

        (VOID)swprintf_s(
            Text,
            ARRAYSIZE(Text),
            L"%lu",
            (unsigned long)B2lMtContacts[Index].Id
            );
        Label = Circle;
        (VOID)SetTextColor(Dc, RGB(0, 0, 0));
        (VOID)DrawTextW(Dc, Text, -1, &Label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    (VOID)SetTextColor(Dc, RGB(180, 180, 180));
    TextRect.left = 24;
    TextRect.top = Client.bottom - 62;
    TextRect.right = Client.right - 24;
    TextRect.bottom = Client.bottom - 12;
    (VOID)DrawTextW(
        Dc,
        L"Iki veya daha fazla parmaga ayni anda dokun. Sayac ve ayri ID'ler gorunmeli.",
        -1,
        &TextRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE
        );

    (VOID)SelectObject(Dc, OldFont);
    if (DetailFont != NULL) {
        (VOID)DeleteObject(DetailFont);
    }
    if (HeaderFont != NULL) {
        (VOID)DeleteObject(HeaderFont);
    }
    if (Background != NULL) {
        (VOID)DeleteObject(Background);
    }
    EndPaint(Window, &Paint);
}

static LRESULT
B2lMtHandlePointer(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    POINTER_INFO PointerInfo;
    POINT Position;
    UINT32 PointerId;
    BOOL Active;

    if ((B2lMtInputMode != B2lMtInputNone) &&
        (B2lMtInputMode != B2lMtInputPointer)) {
        return 0;
    }
    PointerId = GET_POINTERID_WPARAM(WParam);
    Position.x = GET_X_LPARAM(LParam);
    Position.y = GET_Y_LPARAM(LParam);
    ZeroMemory(&PointerInfo, sizeof(PointerInfo));
    if (GetPointerInfo(PointerId, &PointerInfo)) {
        if ((PointerInfo.pointerType != PT_TOUCH) &&
            (PointerInfo.pointerType != PT_POINTER)) {
            return 0;
        }
        Position = PointerInfo.ptPixelLocation;
    }
    B2lMtInputMode = B2lMtInputPointer;
    Active = (Message != WM_POINTERUP);
    if ((PointerInfo.pointerFlags & POINTER_FLAG_CANCELED) != 0U) {
        Active = FALSE;
    }
    if (Active) {
        POINT ClientPosition = Position;
        (VOID)ScreenToClient(Window, &ClientPosition);
        if (B2lMtCloseHit(Window, ClientPosition)) {
            (VOID)DestroyWindow(Window);
            return 0;
        }
    }
    B2lMtUpdateContact(Window, PointerId, Position, Active);
    return 0;
}

static LRESULT
B2lMtHandleTouch(HWND Window, WPARAM WParam, LPARAM LParam)
{
    TOUCHINPUT Inputs[B2L_MT_MAX_CONTACTS];
    HTOUCHINPUT Handle = (HTOUCHINPUT)LParam;
    UINT Count = (UINT)LOWORD(WParam);
    UINT Index;

    if ((B2lMtInputMode != B2lMtInputNone) &&
        (B2lMtInputMode != B2lMtInputTouch)) {
        (VOID)CloseTouchInputHandle(Handle);
        return 0;
    }
    if (Count > B2L_MT_MAX_CONTACTS) {
        Count = B2L_MT_MAX_CONTACTS;
    }
    ZeroMemory(Inputs, sizeof(Inputs));
    if (GetTouchInputInfo(Handle, Count, Inputs, (INT)sizeof(TOUCHINPUT))) {
        B2lMtInputMode = B2lMtInputTouch;
        for (Index = 0U; Index < Count; ++Index) {
            POINT Position;
            BOOL Active;

            Position.x = Inputs[Index].x / 100;
            Position.y = Inputs[Index].y / 100;
            Active = ((Inputs[Index].dwFlags & TOUCHEVENTF_UP) == 0U);
            if (Active) {
                POINT ClientPosition = Position;
                (VOID)ScreenToClient(Window, &ClientPosition);
                if (B2lMtCloseHit(Window, ClientPosition)) {
                    (VOID)CloseTouchInputHandle(Handle);
                    (VOID)DestroyWindow(Window);
                    return 0;
                }
            }
            B2lMtUpdateContact(Window, Inputs[Index].dwID, Position, Active);
        }
    }
    (VOID)CloseTouchInputHandle(Handle);
    return 0;
}

static LRESULT CALLBACK
B2lMtWindowProcedure(
    HWND Window,
    UINT Message,
    WPARAM WParam,
    LPARAM LParam
    )
{
    POINT Position;

    switch (Message) {
    case WM_PAINT:
        B2lMtPaint(Window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
        return B2lMtHandlePointer(Window, Message, WParam, LParam);
    case WM_POINTERCAPTURECHANGED:
        if (B2lMtInputMode == B2lMtInputPointer) {
            B2L_MT_CONTACT *Contact = B2lMtFindContact(
                GET_POINTERID_WPARAM(WParam),
                FALSE
                );
            if (Contact != NULL) {
                Contact->Active = FALSE;
                (VOID)InvalidateRect(Window, NULL, FALSE);
            }
        }
        return 0;
    case WM_TOUCH:
        return B2lMtHandleTouch(Window, WParam, LParam);
    case WM_LBUTTONDOWN:
        if (B2lMtInputMode == B2lMtInputNone) {
            B2lMtInputMode = B2lMtInputMouse;
            Position.x = GET_X_LPARAM(LParam);
            Position.y = GET_Y_LPARAM(LParam);
            if (B2lMtCloseHit(Window, Position)) {
                (VOID)DestroyWindow(Window);
                return 0;
            }
            (VOID)ClientToScreen(Window, &Position);
            B2lMtUpdateContact(Window, 1U, Position, TRUE);
        }
        return 0;
    case WM_MOUSEMOVE:
        if ((B2lMtInputMode == B2lMtInputMouse) &&
            ((WParam & MK_LBUTTON) != 0U)) {
            Position.x = GET_X_LPARAM(LParam);
            Position.y = GET_Y_LPARAM(LParam);
            (VOID)ClientToScreen(Window, &Position);
            B2lMtUpdateContact(Window, 1U, Position, TRUE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (B2lMtInputMode == B2lMtInputMouse) {
            Position.x = GET_X_LPARAM(LParam);
            Position.y = GET_Y_LPARAM(LParam);
            (VOID)ClientToScreen(Window, &Position);
            B2lMtUpdateContact(Window, 1U, Position, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (WParam == VK_ESCAPE) {
            (VOID)DestroyWindow(Window);
        }
        return 0;
    case WM_CLOSE:
        (VOID)DestroyWindow(Window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(Window, Message, WParam, LParam);
    }
}

INT WINAPI
wWinMain(
    HINSTANCE Instance,
    HINSTANCE PreviousInstance,
    PWSTR CommandLine,
    INT ShowCommand
    )
{
    WNDCLASSEXW WindowClass = { 0 };
    MSG Message;
    HWND Window;
    BOOL MessageResult;
    INT ScreenHeight;
    INT ScreenWidth;

    UNREFERENCED_PARAMETER(PreviousInstance);
    UNREFERENCED_PARAMETER(CommandLine);
    UNREFERENCED_PARAMETER(ShowCommand);

    (VOID)SetProcessDPIAware();
    B2lMtClearContacts();
    WindowClass.cbSize = (UINT)sizeof(WindowClass);
    WindowClass.lpfnWndProc = B2lMtWindowProcedure;
    WindowClass.hInstance = Instance;
    WindowClass.hCursor = LoadCursorW(NULL, IDC_CROSS);
    WindowClass.lpszClassName = B2lMtClassName;
    if (RegisterClassExW(&WindowClass) == 0U) {
        return 1;
    }

    ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
    ScreenHeight = GetSystemMetrics(SM_CYSCREEN);
    if ((ScreenWidth <= 0) || (ScreenHeight <= 0)) {
        return 1;
    }
    Window = CreateWindowExW(
        WS_EX_TOPMOST,
        B2lMtClassName,
        L"Beyond2LTE Multi-touch Test",
        WS_POPUP | WS_VISIBLE,
        0,
        0,
        ScreenWidth,
        ScreenHeight,
        NULL,
        NULL,
        Instance,
        NULL
        );
    if (Window == NULL) {
        return 1;
    }
    (VOID)RegisterTouchWindow(Window, 0UL);
    (VOID)SetWindowPos(
        Window,
        HWND_TOPMOST,
        0,
        0,
        ScreenWidth,
        ScreenHeight,
        SWP_SHOWWINDOW
        );
    (VOID)SetForegroundWindow(Window);
    (VOID)UpdateWindow(Window);

    while ((MessageResult = GetMessageW(&Message, NULL, 0U, 0U)) > 0) {
        (VOID)TranslateMessage(&Message);
        (VOID)DispatchMessageW(&Message);
    }
    return (MessageResult < 0) ? 1 : 0;
}
