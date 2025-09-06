#include "flight_mode.h"
#include <iostream>
#include <sstream>
#include <cmath>

ATSFlightMode* ATSFlightMode::instance = nullptr;

ATSFlightMode::ATSFlightMode() 
    : flight_mode_active(false)
    , input_blocked(false)
    , keyboard_hook(nullptr)
    , mouse_hook(nullptr)
    , mouse_look_active(false)
    , truck_data(nullptr)
    , trailer_data(nullptr) {
    
    ZeroMemory(keys, sizeof(keys));
    
    // Initialize camera
    camera.x = camera.y = camera.z = 0.0f;
    camera.pitch = camera.yaw = camera.roll = 0.0f;
    camera.speed = 50.0f;
    camera.noclip = true;
    
    last_mouse_pos = {0, 0};
}

ATSFlightMode::~ATSFlightMode() {
    Shutdown();
}

ATSFlightMode* ATSFlightMode::GetInstance() {
    if (!instance) {
        instance = new ATSFlightMode();
    }
    return instance;
}

bool ATSFlightMode::Initialize(const scs_telemetry_init_params_t* params) {
    // Find game memory addresses
    if (!FindGameAddresses()) {
        return false;
    }
    
    // Install input hooks
    keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, 
                                   GetModuleHandle(nullptr), 0);
    mouse_hook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, 
                                GetModuleHandle(nullptr), 0);
    
    if (!keyboard_hook || !mouse_hook) {
        return false;
    }
    
    // Register telemetry callbacks
    params->register_for_event(SCS_TELEMETRY_EVENT_frame_end, OnFrameEnd, nullptr);
    
    // Register for truck position updates
    params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, 
                               SCS_U32_NIL, SCS_VALUE_TYPE_dplacement, 
                               SCS_TELEMETRY_CHANNEL_FLAG_none, OnTruckTelemetry, nullptr);
    
    return true;
}

void ATSFlightMode::Shutdown() {
    if (flight_mode_active) {
        ToggleFlightMode(); // Disable flight mode
    }
    
    if (keyboard_hook) {
        UnhookWindowsHookEx(keyboard_hook);
        keyboard_hook = nullptr;
    }
    
    if (mouse_hook) {
        UnhookWindowsHookEx(mouse_hook);
        mouse_hook = nullptr;
    }
}

void ATSFlightMode::ToggleFlightMode() {
    flight_mode_active = !flight_mode_active;
    
    if (flight_mode_active) {
        // Entering flight mode
        SaveVehicleState();
        DisableGameInput();
        
        // Set camera to current truck position
        if (truck_data) {
            camera.x = truck_data->world_placement.position.x;
            camera.y = truck_data->world_placement.position.y + 3.0f; // Slightly above truck
            camera.z = truck_data->world_placement.position.z;
        }
        
        mouse_look_active = true;
        ShowMessage("FLIGHT MODE ACTIVATED", 
                   "Controls:\n"
                   "WASD - Move horizontally\n"
                   "Space/C - Move up/down\n"
                   "Mouse - Look around\n"
                   "Shift - Speed boost\n"
                   "Ctrl - Slow mode\n"
                   "Ctrl+F8 - Teleport truck to camera\n"
                   "F9 - Exit flight mode");
        
    } else {
        // Exiting flight mode
        EnableGameInput();
        mouse_look_active = false;
        ShowMessage("FLIGHT MODE DEACTIVATED", "Normal driving controls restored");
    }
}

void ATSFlightMode::TeleportVehicleToCamera() {
    if (!flight_mode_active) return;
    
    // Calculate ground position
    float ground_y = camera.y - 2.0f; // Place truck on ground below camera
    
    // Write truck position to memory
    if (addresses.vehicle_x && addresses.vehicle_y && addresses.vehicle_z) {
        WriteProcessMemory(GetCurrentProcess(), (LPVOID)addresses.vehicle_x, 
                          &camera.x, sizeof(float), nullptr);
        WriteProcessMemory(GetCurrentProcess(), (LPVOID)addresses.vehicle_y, 
                          &ground_y, sizeof(float), nullptr);
        WriteProcessMemory(GetCurrentProcess(), (LPVOID)addresses.vehicle_z, 
                          &camera.z, sizeof(float), nullptr);
        
        // Set truck rotation to match camera yaw
        float truck_rotation = camera.yaw * (M_PI / 180.0f);
        WriteProcessMemory(GetCurrentProcess(), (LPVOID)addresses.vehicle_rot_y, 
                          &truck_rotation, sizeof(float), nullptr);
    }
    
    ShowMessage("VEHICLE TELEPORTED", "Truck moved to camera position");
}

void ATSFlightMode::UpdateFlightMovement(float delta_time) {
    if (!flight_mode_active) return;
    
    float speed = camera.speed * delta_time;
    
    // Speed modifiers
    if (keys[VK_SHIFT]) speed *= 4.0f;    // Turbo mode
    if (keys[VK_CONTROL]) speed *= 0.2f;  // Precision mode
    
    // Convert angles to radians
    float yaw_rad = camera.yaw * (M_PI / 180.0f);
    float pitch_rad = camera.pitch * (M_PI / 180.0f);
    
    // Calculate movement vectors
    float forward_x = cos(yaw_rad) * cos(pitch_rad);
    float forward_y = sin(pitch_rad);
    float forward_z = sin(yaw_rad) * cos(pitch_rad);
    
    float right_x = cos(yaw_rad + M_PI/2);
    float right_z = sin(yaw_rad + M_PI/2);
    
    // Apply movement
    if (keys['W']) { // Forward
        camera.x += forward_x * speed;
        camera.y += forward_y * speed;
        camera.z += forward_z * speed;
    }
    if (keys['S']) { // Backward
        camera.x -= forward_x * speed;
        camera.y -= forward_y * speed;
        camera.z -= forward_z * speed;
    }
    if (keys['A']) { // Left
        camera.x -= right_x * speed;
        camera.z -= right_z * speed;
    }
    if (keys['D']) { // Right
        camera.x += right_x * speed;
        camera.z += right_z * speed;
    }
    if (keys[VK_SPACE]) { // Up
        camera.y += speed;
    }
    if (keys['C']) { // Down
        camera.y -= speed;
    }
    
    // Update camera in game
    UpdateCameraInGame();
}

LRESULT CALLBACK ATSFlightMode::KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* kbd = (KBDLLHOOKSTRUCT*)lParam;
        ATSFlightMode* flight = GetInstance();
        
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            flight->keys[kbd->vkCode] = true;
            
            // F9 - Toggle flight mode
            if (kbd->vkCode == VK_F9) {
                flight->ToggleFlightMode();
                return 1; // Block the key
            }
            
            // Ctrl+F8 - Teleport vehicle
            if (kbd->vkCode == VK_F8 && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                flight->TeleportVehicleToCamera();
                return 1; // Block the key
            }
            
            // Block game input when in flight mode
            if (flight->flight_mode_active) {
                // Allow only flight mode keys
                if (kbd->vkCode == 'W' || kbd->vkCode == 'A' || 
                    kbd->vkCode == 'S' || kbd->vkCode == 'D' ||
                    kbd->vkCode == VK_SPACE || kbd->vkCode == 'C' ||
                    kbd->vkCode == VK_SHIFT || kbd->vkCode == VK_CONTROL ||
                    kbd->vkCode == VK_F8 || kbd->vkCode == VK_F9) {
                    // Allow these keys for flight mode
                } else {
                    return 1; // Block all other keys
                }
            }
        }
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            flight->keys[kbd->vkCode] = false;
            
            if (flight->flight_mode_active) {
                // Block key release events too
                return 1;
            }
        }
    }
    
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK ATSFlightMode::MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    ATSFlightMode* flight = GetInstance();
    
    if (nCode >= 0 && flight->flight_mode_active && flight->mouse_look_active) {
        if (wParam == WM_MOUSEMOVE) {
            MSLLHOOKSTRUCT* mouse = (MSLLHOOKSTRUCT*)lParam;
            
            static bool first_move = true;
            if (first_move) {
                flight->last_mouse_pos = mouse->pt;
                first_move = false;
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
            
            // Calculate mouse delta
            int delta_x = mouse->pt.x - flight->last_mouse_pos.x;
            int delta_y = mouse->pt.y - flight->last_mouse_pos.y;
            
            // Update camera rotation
            flight->camera.yaw += delta_x * 0.15f;
            flight->camera.pitch -= delta_y * 0.15f;
            
            // Clamp pitch
            if (flight->camera.pitch > 89.0f) flight->camera.pitch = 89.0f;
            if (flight->camera.pitch < -89.0f) flight->camera.pitch = -89.0f;
            
            // Wrap yaw
            if (flight->camera.yaw > 360.0f) flight->camera.yaw -= 360.0f;
            if (flight->camera.yaw < 0.0f) flight->camera.yaw += 360.0f;
            
            flight->last_mouse_pos = mouse->pt;
            
            // Center cursor to prevent it from leaving the window
            RECT rect;
            HWND game_window = GetForegroundWindow();
            GetClientRect(game_window, &rect);
            POINT center = {rect.right / 2, rect.bottom / 2};
            ClientToScreen(game_window, &center);
            SetCursorPos(center.x, center.y);
            flight->last_mouse_pos = center;
            
            return 1; // Block mouse movement from reaching the game
        }
        
        // Block all mouse clicks in flight mode
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || 
            wParam == WM_MBUTTONDOWN || wParam == WM_LBUTTONUP || 
            wParam == WM_RBUTTONUP || wParam == WM_MBUTTONUP) {
            return 1;
        }
    }
    
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

SCSAPI_VOID ATSFlightMode::OnFrameEnd(const scs_telemetry_frame_end_t* const info) {
    ATSFlightMode* flight = GetInstance();
    static DWORD last_time = GetTickCount();
    DWORD current_time = GetTickCount();
    float delta_time = (current_time - last_time) / 1000.0f;
    last_time = current_time;
    
    flight->UpdateFlightMovement(delta_time);
}

SCSAPI_VOID ATSFlightMode::OnTruckTelemetry(const scs_string_t channel, const scs_u32_t index,
                                          const scs_value_t* const value, const scs_context_t context) {
    ATSFlightMode* flight = GetInstance();
    
    if (strcmp(channel, SCS_TELEMETRY_TRUCK_CHANNEL_world_placement) == 0) {
        flight->truck_data = &value->value_dplacement;
    }
}

bool ATSFlightMode::FindGameAddresses() {
    // This would need to be implemented with pattern scanning
    // For now, using placeholder addresses
    HMODULE game_module = GetModuleHandle(L"amtrucks.exe");
    if (!game_module) return false;
    
    addresses.base_address = (uintptr_t)game_module;
    
    // These addresses would need to be found through reverse engineering
    // Using placeholders for now
    addresses.camera_x = addresses.base_address + 0x1234567;
    addresses.camera_y = addresses.base_address + 0x1234568;
    addresses.camera_z = addresses.base_address + 0x1234569;
    // ... etc
    
    return true;
}

void ATSFlightMode::UpdateCameraInGame() {
    if (!addresses.camera_x) return;
    
    // Write camera position to game memory
    WriteProcessMemory(GetCurrentProcess(), (LPVOID)addresses.camera_x, 
                      &camera.x, sizeof(float), nullptr);
    WriteProcessMemory(GetCurrentProcess(), (LPVOID)addresses.camera_y, 
                      &camera.y, sizeof(float), nullptr);
    WriteProcessMemory(GetCurrentProcess(), (LPVOID)addresses.camera_z, 
                      &camera.z, sizeof(float), nullptr);
    WriteProcessMemory(GetCurrentProcess(), (LPVOID)addresses.camera_pitch, 
                      &camera.pitch, sizeof(float), nullptr);
    WriteProcessMemory(GetCurrentProcess(), (LPVOID)addresses.camera_yaw, 
                      &camera.yaw, sizeof(float), nullptr);
}

void ATSFlightMode::ShowMessage(const char* title, const char* message) {
    // Convert to wide strings for MessageBox
    int title_len = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
    int msg_len = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
    
    wchar_t* w_title = new wchar_t[title_len];
    wchar_t* w_message = new wchar_t[msg_len];
    
    MultiByteToWideChar(CP_UTF8, 0, title, -1, w_title, title_len);
    MultiByteToWideChar(CP_UTF8, 0, message, -1, w_message, msg_len);
    
    MessageBox(nullptr, w_message, w_title, MB_OK | MB_ICONINFORMATION);
    
    delete[] w_title;
    delete[] w_message;
}

// Plugin entry points
extern "C" {
    SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const params) {
        if (version != SCS_TELEMETRY_VERSION_1_01) {
            return SCS_RESULT_unsupported;
        }
        
        ATSFlightMode* flight = ATSFlightMode::GetInstance();
        if (!flight->Initialize(params)) {
            return SCS_RESULT_generic_error;
        }
        
        return SCS_RESULT_ok;
    }
    
    SCSAPI_VOID scs_telemetry_shutdown(void) {
        ATSFlightMode* flight = ATSFlightMode::GetInstance();
        flight->Shutdown();
        delete flight;
        ATSFlightMode::instance = nullptr;
    }
}

// DLL Entry Point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
