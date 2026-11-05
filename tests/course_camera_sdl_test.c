#define SDL_MAIN_HANDLED 1
#include "desktop/host_main.c"
#include <assert.h>
/* Synthetic focus events enter SDL's real window event machinery. The
 * dummy backend cannot acquire OS focus; no production API is replaced. */
extern bool SDL_SendWindowEvent(SDL_Window *, SDL_EventType, int, int);
static void Pump(void) {
  SDL_UpdateJoysticks();
  SDL_Event event;
  while (SDL_PollEvent(&event)) HandleDeviceEvent(&event);
}
static SDL_Joystick *Pad(const char *name, SDL_JoystickID *id) {
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = 15;
  desc.name = name;
  *id = SDL_AttachVirtualJoystick(&desc);
  assert(*id);
  SDL_Joystick *pad = SDL_OpenJoystick(*id);
  assert(pad);
  return pad;
}
static void Axes(SDL_Joystick *pad, int x, int y, bool click) {
  assert(SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX, (Sint16)x));
  assert(SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY, (Sint16)y));
  assert(SDL_SetJoystickVirtualButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK, click));
  Pump();
}
int main(int argc, char **argv) {
  assert(argc == 2);
  assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_VIDEODRIVER", "dummy", true));
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD));
  ParseConfigFile(argv[1]);
  static const SnesDesktopHostGame game = {.num_players = 2};
  g_game = &game;
  for (unsigned i=0;i<kSnesHostPadSeats;++i) g_gamepad[i].joystick_id=(SDL_JoystickID)-1;
  g_window=SDL_CreateWindow("Camera input regression",256,224,SDL_WINDOW_HIDDEN);
  assert(g_window);
  assert(!snesrecomp_desktop_input_active());
  SDL_SendWindowEvent(g_window, SDL_EVENT_WINDOW_FOCUS_GAINED, 0, 0);
  assert(snesrecomp_desktop_input_active());
  SDL_JoystickID first_id, second_id;
  SDL_Joystick *first=Pad("camera first",&first_id);
  SDL_Joystick *second=Pad("camera second",&second_id);
  /* Reverse the enumeration order: explicit launcher seat selection wins. */
  g_seat_instance[0]=second_id+1;
  g_seat_instance[1]=first_id+1;
  Pump();
  assert(g_gamepad[0].joystick_id==second_id);
  assert(g_gamepad[1].joystick_id==first_id);
  SnesDesktopAnalogInput input;
  Axes(second,25000,-20000,true);
  assert(snesrecomp_desktop_analog_input(0,&input));
  assert(input.right_x==25000 && input.right_y==-20000 && input.right_stick);
  assert(g_input_state==0 && g_pad_buttons==0 && g_gamepad[0].axis_buttons==0);
  Axes(first,-12345,17000,false);
  assert(snesrecomp_desktop_analog_input(1,&input) && input.right_x==-12345);
  assert(snesrecomp_desktop_analog_input(0,&input) && input.right_x==25000);
  g_config.player_src[0]=1;
  assert(!snesrecomp_desktop_analog_input(0,&input) && input.right_x==0);
  g_config.player_src[0]=2;
  g_config.enable_gamepad[0]=false;
  assert(!snesrecomp_desktop_analog_input(0,&input));
  g_config.enable_gamepad[0]=true;
  g_paused=1;
  assert(!snesrecomp_desktop_analog_input(0,&input));
  g_paused=0;
  g_overlay_modal=true;
  Axes(second,20000,-10000,false);
  assert(!snesrecomp_desktop_analog_input(0,&input));
  g_overlay_modal=false;
  OverlayNoteClosed();
  assert(!snesrecomp_desktop_analog_input(0,&input));
  Axes(second,0,0,false);
  assert(snesrecomp_desktop_analog_input(0,&input) && input.right_x==0);
  Axes(second,16000,12000,true);
  assert(snesrecomp_desktop_analog_input(0,&input) && input.right_stick);
  SDL_SendWindowEvent(g_window,SDL_EVENT_WINDOW_FOCUS_LOST,0,0);
  assert(!snesrecomp_desktop_analog_input(0,&input) && input.right_x==0);
  Axes(second,18000,10000,false);
  SDL_SendWindowEvent(g_window,SDL_EVENT_WINDOW_FOCUS_GAINED,0,0);
  assert(!snesrecomp_desktop_analog_input(0,&input));
  Axes(second,0,0,false);
  assert(snesrecomp_desktop_analog_input(0,&input));
  g_host_replaying_frame=true;
  assert(!snesrecomp_desktop_analog_input(0,&input));
  g_host_replaying_frame=false;
  Axes(first,0,0,false);
  assert(snesrecomp_desktop_analog_input(1,&input));
  Axes(first,-12345,17000,false);
  assert(g_input_state==0 && g_pad_buttons==0 && g_gamepad[0].axis_buttons==0);
  assert(SDL_SetJoystickVirtualAxis(second,SDL_GAMEPAD_AXIS_LEFTX,25000));
  Pump();
  assert(g_gamepad[0].axis_buttons != 0);
  unsigned guest=g_gamepad[0].axis_buttons;
  Axes(second,-24000,29000,false);
  assert(g_gamepad[0].axis_buttons==guest);
  assert(SDL_SetJoystickVirtualAxis(second,SDL_GAMEPAD_AXIS_LEFTX,0));
  Axes(second,0,0,false);
  FILE *config=fopen(argv[1], "w");
  assert(config);
  fputs("[Controller]\nSourceP1 = 2\nSourceP2 = 2\n[GamepadMap]\n"
        "EnableGamepad1 = true\nEnableGamepad2 = true\n"
        "Controls = R3,DpadDown,DpadLeft,DpadRight,Back,Start,B,A,Y,X,Lb,Rb\n",config);
  fclose(config);
  ParseConfigFile(argv[1]);
  Axes(second,0,0,true);
  assert(snesrecomp_desktop_analog_input(0,&input) && !input.right_stick);
  assert(g_pad_buttons & (1u << 4)); /* A custom SNES binding wins over camera reset. */
  Axes(second,0,0,false);
  assert(g_pad_buttons==0);
  assert(SDL_DetachVirtualJoystick(second_id));
  Pump();
  assert(!snesrecomp_desktop_analog_input(0,&input) && input.right_x==0 && !input.right_stick);
  assert(snesrecomp_desktop_analog_input(1,&input) && input.right_x==-12345);
  SDL_CloseJoystick(first);
  SDL_CloseJoystick(second);
  SDL_DetachVirtualJoystick(first_id);
  SDL_DestroyWindow(g_window);
  g_window=NULL;
  SDL_Quit();
  puts("PASS: SDL virtual pads, selected seats, right axes/R3, remapping, guest isolation, pause/modal/focus/replay/disconnect");
  return 0;
}
