#ifndef FLIGHT_MODE_H
#define FLIGHT_MODE_H

#include <windows.h>
#include <vector>
#include <memory>

// SCS SDK includes
#include "scssdk_telemetry.h"
#include "amtrucks/scssdk_ats.h"
#include "amtrucks/scssdk_telemetry_ats.h"

struct FlightCamera {
    float x, y, z;
    float pitch, yaw, roll;
    float speed;
    bool noclip;
};

struct VehicleState {
    float x, y, z;
    float rotation_x, rotation_y, rotation_z;
    float speed;
    bool engine_enabled;
};

class ATSFlightMode {
private:
    static ATSFlightMode* instance;
    
    // Flight mode state
    bool flight_mode_active;
    bool input_blocked;
    FlightCamera camera;
    VehicleState saved_vehicle_state;
    
    // Input handling
    HHOOK keyboard_hook;
    HHOOK mouse_hook;
    bool keys[256];
    POINT last_mouse_pos;
    bool mouse_look_active;
    
    // Game memory addresses (found through reverse engineering)
    struct GameAddresses {
        uintptr_t base_address;
        uintptr_t camera_x;
        uintptr_t camera_y;
        uintptr_t camera_z;
        uintptr_t camera_pitch;
        uintptr_t camera_yaw;
        uintptr_t camera_roll;
        uintptr_t vehicle_x;
        uintptr_t vehicle_y;
        uintptr_t vehicle_z;
        uintptr_t vehicle_rot_x;
        uintptr_t vehicle_rot_y;
        uintptr_t vehicle_rot_z;
        uintptr_t input_enabled;
        uintptr_t camera_mode;
    } addresses;
    
    // Telemetry data
    const scs_telemetry_truck_t* truck_data;
    const scs_telemetry_trailer_t* trailer_data;
    
public:
    ATSFlightMode();
    ~ATSFlightMode();
    
    static ATSFlightMode* GetInstance();
    
    bool Initialize(const scs_telemetry_init_params_t* params);
    void Shutdown();
    
    void ToggleFlightMode();
    void TeleportVehicleToCamera();
    void UpdateFlightMovement(float delta_time);
    void HandleInput();
    
    // Hook procedures
    static LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    
    // Telemetry callbacks
    static SCSAPI_VOID OnFrameEnd(const scs_telemetry_frame_end_t* const info);
    static SCSAPI_VOID OnTruckTelemetry(const scs_string_t channel, const scs_u32_t index, 
                                       const scs_value_t* const value, const scs_context_t context);
    
private:
    bool FindGameAddresses();
    void SaveVehicleState();
    void RestoreVehicleState();
    void DisableGameInput();
    void EnableGameInput();
    void UpdateCameraInGame();
    void ShowFlightModeHUD();
};

#endif // FLIGHT_MODE_H
