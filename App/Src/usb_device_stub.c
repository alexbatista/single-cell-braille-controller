// ----------------------------------------------------------------------------
// usb_device_stub.c
//
// The MODBUS variant excludes USB_DEVICE/ from the build, but two references
// to it survive in generated code that cannot be guarded:
//
//   * Core/Src/main.c calls MX_USB_DEVICE_Init() from the init sequence.
//   * Core/Src/stm32f1xx_it.c's USB_LP_CAN1_RX0_IRQHandler calls
//     HAL_PCD_IRQHandler(&hpcd_USB_FS), and the vector table keeps that
//     handler alive through --gc-sections.
//
// Both sit outside any USER CODE region, so guarding them there would be
// undone by the next CubeMX code generation. No-op definitions here satisfy
// the link instead, and leave the generated files untouched.
//
// The handler can never actually run: nothing enables the USB interrupt in
// this variant, because MX_USB_DEVICE_Init() is the no-op below.
// ----------------------------------------------------------------------------

#if defined(BRAILLE_INPUT_MODBUS)

#include "stm32f1xx_hal.h"

/** @brief Stands in for USB_DEVICE/App/usb_device.c's initialiser. */
void MX_USB_DEVICE_Init(void) {}

/** @brief Stands in for USB_DEVICE/Target/usbd_conf.c's PCD handle. */
PCD_HandleTypeDef hpcd_USB_FS;

#endif // BRAILLE_INPUT_MODBUS
