using System.Runtime.InteropServices;

namespace KarPurLinux;

sealed class LinuxGamepad : IDisposable
{
    const uint InitGamepad = 0x00002000;
    const int South = 0, Start = 6, LeftShoulder = 9, RightShoulder = 10,
        DpadUp = 11, DpadDown = 12, DpadLeft = 13, DpadRight = 14;
    const int LeftX = 0, LeftY = 1;
    const uint Up = 1 << 0, Down = 1 << 1, Left = 1 << 2, Right = 1 << 3,
        Confirm = 1 << 4, Back = 1 << 5, Menu = 1 << 6, Previous = 1 << 7, Next = 1 << 8;
    const short DeadZone = 16000;

    IntPtr gamepad;
    bool initialized;

    [DllImport("SDL3", EntryPoint = "SDL_InitSubSystem", CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    static extern bool SdlInitSubSystem(uint flags);

    [DllImport("SDL3", EntryPoint = "SDL_QuitSubSystem", CallingConvention = CallingConvention.Cdecl)]
    static extern void SdlQuitSubSystem(uint flags);

    [DllImport("SDL3", EntryPoint = "SDL_PumpEvents", CallingConvention = CallingConvention.Cdecl)]
    static extern void SdlPumpEvents();

    [DllImport("SDL3", EntryPoint = "SDL_GetGamepads", CallingConvention = CallingConvention.Cdecl)]
    static extern IntPtr SdlGetGamepads(out int count);

    [DllImport("SDL3", EntryPoint = "SDL_OpenGamepad", CallingConvention = CallingConvention.Cdecl)]
    static extern IntPtr SdlOpenGamepad(int instanceId);

    [DllImport("SDL3", EntryPoint = "SDL_GamepadConnected", CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    static extern bool SdlGamepadConnected(IntPtr pad);

    [DllImport("SDL3", EntryPoint = "SDL_CloseGamepad", CallingConvention = CallingConvention.Cdecl)]
    static extern void SdlCloseGamepad(IntPtr pad);

    [DllImport("SDL3", EntryPoint = "SDL_GetGamepadButton", CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    static extern bool SdlGetGamepadButton(IntPtr pad, int button);

    [DllImport("SDL3", EntryPoint = "SDL_GetGamepadAxis", CallingConvention = CallingConvention.Cdecl)]
    static extern short SdlGetGamepadAxis(IntPtr pad, int axis);

    [DllImport("SDL3", EntryPoint = "SDL_free", CallingConvention = CallingConvention.Cdecl)]
    static extern void SdlFree(IntPtr memory);

    [DllImport("SDL3", EntryPoint = "SDL_GetError", CallingConvention = CallingConvention.Cdecl)]
    static extern IntPtr SdlGetError();

    public bool Initialize(out string error)
    {
        try
        {
            initialized = SdlInitSubSystem(InitGamepad);
            error = initialized ? "" : SdlError();
            return initialized;
        }
        catch (DllNotFoundException e)
        {
            error = "SDL3 could not be loaded: " + e.Message;
            return false;
        }
        catch (EntryPointNotFoundException e)
        {
            error = "SDL3 gamepad support is unavailable: " + e.Message;
            return false;
        }
    }

    public bool TryRead(out uint buttons)
    {
        buttons = 0;
        if (!initialized) return false;

        SdlPumpEvents();
        if (gamepad != IntPtr.Zero && !SdlGamepadConnected(gamepad))
        {
            SdlCloseGamepad(gamepad);
            gamepad = IntPtr.Zero;
        }
        if (gamepad == IntPtr.Zero) OpenFirstConnected();
        if (gamepad == IntPtr.Zero) return false;

        if (SdlGetGamepadButton(gamepad, DpadUp)) buttons |= Up;
        if (SdlGetGamepadButton(gamepad, DpadDown)) buttons |= Down;
        if (SdlGetGamepadButton(gamepad, DpadLeft)) buttons |= Left;
        if (SdlGetGamepadButton(gamepad, DpadRight)) buttons |= Right;
        if (SdlGetGamepadButton(gamepad, South)) buttons |= Confirm;
        if (SdlGetGamepadButton(gamepad, 1)) buttons |= Back;
        if (SdlGetGamepadButton(gamepad, Start)) buttons |= Menu;
        if (SdlGetGamepadButton(gamepad, LeftShoulder)) buttons |= Previous;
        if (SdlGetGamepadButton(gamepad, RightShoulder)) buttons |= Next;

        short x = SdlGetGamepadAxis(gamepad, LeftX), y = SdlGetGamepadAxis(gamepad, LeftY);
        buttons |= Directions(x, y);
        return true;
    }

    void OpenFirstConnected()
    {
        IntPtr ids = SdlGetGamepads(out int count);
        if (ids == IntPtr.Zero) return;
        try
        {
            for (int i = 0; i < count; i++)
            {
                gamepad = SdlOpenGamepad(Marshal.ReadInt32(ids, i * sizeof(int)));
                if (gamepad != IntPtr.Zero) return;
            }
        }
        finally { SdlFree(ids); }
    }

    static string SdlError() => Marshal.PtrToStringUTF8(SdlGetError()) ?? "unknown SDL3 error";

    public static uint Directions(short x, short y)
    {
        uint result = 0;
        if (y < -DeadZone) result |= Up;
        if (y > DeadZone) result |= Down;
        if (x < -DeadZone) result |= Left;
        if (x > DeadZone) result |= Right;
        return result;
    }

    public static uint NewlyPressed(uint current, uint previous) => current & ~previous;

    public void Dispose()
    {
        if (gamepad != IntPtr.Zero)
        {
            SdlCloseGamepad(gamepad);
            gamepad = IntPtr.Zero;
        }
        if (initialized)
        {
            SdlQuitSubSystem(InitGamepad);
            initialized = false;
        }
    }

    public const uint UpButton = Up, DownButton = Down, LeftButton = Left, RightButton = Right,
        ConfirmButton = Confirm, BackButton = Back, MenuButton = Menu, PreviousButton = Previous, NextButton = Next;
}
