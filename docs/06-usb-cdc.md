# 6. USB CDC (virtual COM port) — how the whole path actually works

This guide explains the USB side of the firmware: why the `Middlewares/` and
`USB_DEVICE/` folders exist, how the USB stack was turned on, what runs in
interrupt context versus in the main loop, and a line-by-line reading of
`CDC_Receive_FS()` / `CDC_Transmit_FS()` / `CDC_ReadChar()` / `App_run()`.

Files to keep open while reading:

- [USB_DEVICE/App/usbd_cdc_if.c](../USB_DEVICE/App/usbd_cdc_if.c) — your code
- [USB_DEVICE/App/usb_device.c](../USB_DEVICE/App/usb_device.c) — init
- [USB_DEVICE/Target/usbd_conf.c](../USB_DEVICE/Target/usbd_conf.c) — glue to HAL
- [App/Src/app_main.c](../App/Src/app_main.c) — the consumer
- [Core/Src/stm32f1xx_it.c](../Core/Src/stm32f1xx_it.c#L208) — the ISR

---

## 6.0 Short answers first

| Question | Answer |
|---|---|
| Why do `Middlewares/` and `USB_DEVICE/` exist? | `Middlewares/` is **ST's unmodified USB Device Library** (vendor code, generic across all STM32). `USB_DEVICE/` is the **CubeMX-generated project glue** that binds that generic library to *this* MCU and *this* application. See 6.1. |
| Is USB interrupt-driven? | **Yes, entirely.** Enumeration, control transfers, RX and TX completion all happen inside `USB_LP_CAN1_RX0_IRQHandler` at NVIC priority 0. See 6.3. |
| How was it enabled? | `.ioc` (USB peripheral + `USB_DEVICE` class CDC + NVIC line + 48 MHz USB clock) → generated code → `MX_USB_DEVICE_Init()` in `main()` → `HAL_NVIC_EnableIRQ()` inside `HAL_PCD_MspInit()`. See 6.2. |
| What does `App_run()` contribute? | Nothing to USB itself. It is only the **consumer** end of a ring buffer the ISR fills. See 6.5. |
| Why doesn't `uint8_t c` need to be `static`? | Because nothing keeps a pointer to it after `CDC_Transmit_FS()` returns — on the STM32F1 USB IP the bytes are copied into the peripheral's PMA **synchronously** inside that call. Details *and the one case where a stack buffer would be a real bug* in 6.6. |
| Does the MCU sleep and wake on USB? | **No.** There is no `__WFI()` anywhere. The CPU spins in `while (1) { App_run(); }` at 100% duty. It *looks* like sleeping because `App_run()` returns immediately when there's nothing to read. See 6.7. |

---

## 6.1 Why `Middlewares/` and `USB_DEVICE/` are two separate folders

CubeMX splits every middleware into two layers, and it does so on purpose:

```
Middlewares/ST/STM32_USB_Device_Library/     ← vendor code, DO NOT EDIT
├── Core/Src/usbd_core.c        USB state machine: reset, address, configure
│   Core/Src/usbd_ctlreq.c      standard control requests (GET_DESCRIPTOR, SET_ADDRESS…)
│   Core/Src/usbd_ioreq.c       helpers for control-endpoint data stages
└── Class/CDC/Src/usbd_cdc.c    the CDC/ACM class: descriptors, EP1 IN/OUT plumbing

USB_DEVICE/                                  ← generated for THIS project, you edit here
├── App/usb_device.c            MX_USB_DEVICE_Init(): assembles + starts the stack
├── App/usbd_desc.c             VID/PID/strings ("STMicroelectronics Virtual COM Port")
├── App/usbd_cdc_if.c           YOUR callbacks: Init/DeInit/Control/Receive + Transmit
└── Target/usbd_conf.c          binds the generic library to the STM32F1 HAL PCD driver
```

The reasoning:

- **`Middlewares/` is portable and generic.** `usbd_cdc.c` knows what a CDC
  interface descriptor looks like, but nothing about STM32F103, about your
  clock tree, or about braille discs. It's byte-identical across every STM32
  project. It calls out through function pointers (`USBD_LL_*`,
  `pdev->pUserData->Receive`, …) and expects *someone else* to implement them.
- **`USB_DEVICE/Target/usbd_conf.c` provides those `USBD_LL_*` implementations**
  by forwarding them to `HAL_PCD_*` (the F1 USB device driver in `Drivers/`).
  This is the only file that knows you're on an F1. Port to an F4 and this is
  what changes.
- **`USB_DEVICE/App/usbd_cdc_if.c` provides the application hooks.** This is
  the *one* USB file that is genuinely yours — your ring buffer and
  `CDC_ReadChar()` live here, between the `/* USER CODE BEGIN … END */`
  markers so CubeMX regeneration doesn't wipe them.

So: three ownership levels — **ST generic** (`Middlewares/`), **ST
MCU-specific glue** (`USB_DEVICE/Target/`), **yours** (`USB_DEVICE/App/`).
It's the same layering idea as
[01-architecture-layers.md](01-architecture-layers.md), applied to USB.

Both folders show as untracked (`??`) in git because they were generated after
the last commit — they *should* be committed; they're part of the build (see
6.2.3).

---

## 6.2 How USB was enabled — the four things that had to happen

### 6.2.1 In the `.ioc` (CubeMX)

```
Mcu.IP7=USB                      the peripheral itself
Mcu.IP8=USB_DEVICE               the middleware on top of it
USB_DEVICE.VirtualMode=Cdc       class = CDC (virtual COM port)
USB_DEVICE.CLASS_NAME_FS=CDC
PA11.Signal=USB_DM               fixed pins on F103 — not remappable
PA12.Signal=USB_DP
NVIC.USB_LP_CAN1_RX0_IRQn=true\:0\:0\:…      interrupt enabled, prio 0, sub 0
RCC.USBFreq_Value=48000000       ← the non-negotiable one
```

### 6.2.2 The 48 MHz rule (and why the clock tree changed)

USB full-speed needs **exactly 48 MHz** on the USB peripheral clock; it is not
tolerant. On F103 the USB clock can only come from the PLL, divided by 1 or
1.5. So `SystemClock_Config()` in [Core/Src/main.c](../Core/Src/main.c#L116)
now reads:

```
HSE 8 MHz → PLL ×6 → PLLCLK = 48 MHz → SYSCLK = HCLK = 48 MHz
                                   └──→ RCC_USBCLKSOURCE_PLL (÷1) = 48 MHz  ✔
```

> ⚠️ **Note:** [02-execution-flow.md](02-execution-flow.md) still says
> "PLL ×4 → SYSCLK = 32 MHz". That was true before USB was added and is now
> stale — adding USB forced the jump to 48 MHz. Anything in the older guides
> that derives a timing number from 32 MHz (timer prescalers, step rates) is
> off by 1.5× and worth re-checking.

### 6.2.3 In CMake

[cmake/stm32cubemx/CMakeLists.txt](../cmake/stm32cubemx/CMakeLists.txt) gained
a whole second object library so the vendor code compiles separately from your
project code:

```cmake
set(USB_Device_Library_Src  … usbd_core.c usbd_ctlreq.c usbd_ioreq.c usbd_cdc.c)
add_library(USB_Device_Library OBJECT)
target_link_libraries(USB_Device_Library PUBLIC stm32cubemx)
```

plus the four `USB_DEVICE/**` sources and the two include dirs. Note it
deliberately does **not** compile `*_template.c` — `usbd_cdc_if_template.c`,
`usbd_conf_template.c` and `usbd_desc_template.c` in `Middlewares/` are
reference examples, not build inputs. (They're a useful read, though: they show
what ST expects each hook to do.)

### 6.2.4 At runtime — `MX_USB_DEVICE_Init()`

[Core/Src/main.c:96](../Core/Src/main.c#L96) calls it *before* `App_init()`, so
the device is already enumerating while the motors are being configured:

```c
USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS);              // 1
USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC);               // 2
USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS); // 3
USBD_Start(&hUsbDeviceFS);                                  // 4
```

1. **`USBD_Init`** zeroes the handle, stores the descriptor callbacks
   (`usbd_desc.c` → VID `0x0483`, PID `0x5740`), then calls `USBD_LL_Init()` in
   `usbd_conf.c`, which fills `hpcd_USB_FS`, calls `HAL_PCD_Init()` — and
   `HAL_PCD_Init()` calls **`HAL_PCD_MspInit()`**, which is where the interrupt
   is actually armed:

   ```c
   __HAL_RCC_USB_CLK_ENABLE();
   HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 0, 0);   // highest priority
   HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
   ```

   `USBD_LL_Init()` also carves up the 512-byte **PMA** (the USB peripheral's
   dedicated packet RAM) with `HAL_PCDEx_PMAConfig()` — one region per endpoint:
   EP0 OUT `0x18`, EP0 IN `0x58`, EP1 IN `0xC0`, EP1 OUT `0x110`, EP2 IN
   `0x100`. Those addresses are inside the USB block, not in your 20 KB SRAM.

2. **`USBD_RegisterClass`** sets `pdev->pClass = &USBD_CDC`, i.e. installs the
   vtable `{Init, DeInit, Setup, DataIn, DataOut, …}` from `usbd_cdc.c`. This is
   how the generic core later reaches CDC-specific behaviour without knowing
   about CDC.

3. **`USBD_CDC_RegisterInterface`** sets `pdev->pUserData =
   &USBD_Interface_fops_FS` — *your* four functions
   ([usbd_cdc_if.c:143](../USB_DEVICE/App/usbd_cdc_if.c#L143)):

   ```c
   USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = {
     CDC_Init_FS, CDC_DeInit_FS, CDC_Control_FS, CDC_Receive_FS
   };
   ```

   This is the whole answer to "how does ST's library know to call *my*
   function": it doesn't, it calls a function pointer you registered here.

4. **`USBD_Start`** → `USBD_LL_Start` → `HAL_PCD_Start()` → enables the USB
   transceiver, pulls D+ up, unmasks the peripheral's interrupt sources. From
   this instant on, **everything else is interrupt-driven.** Enumeration
   (`GET_DESCRIPTOR`, `SET_ADDRESS`, `SET_CONFIGURATION`) completes in the ISR
   with zero help from the main loop — which is why `lsusb` shows the device
   even when `App_run()` is stuck in a multi-second motor move.

There are two more registration steps you didn't have to write, worth knowing:
`CDC_Init_FS()` is called by the class layer at `SET_CONFIGURATION` time and
does `USBD_CDC_SetTxBuffer(...UserTxBufferFS, 0)` /
`USBD_CDC_SetRxBuffer(...UserRxBufferFS)`. `UserRxBufferFS[1024]` /
`UserTxBufferFS[1024]` are the CubeMX default staging buffers.
**Only the RX one is actually used in this firmware** — `CDC_Transmit_FS()`
takes your pointer directly and never touches `UserTxBufferFS`, so those
1024 bytes are dead weight in a 20 KB RAM budget. `UserRxBufferFS` matters: it
is where the hardware deposits incoming packets, and it's what `Buf` points at
inside `CDC_Receive_FS`.

---

## 6.3 The interrupt anatomy — full call chain, both directions

### RX: host types a key → your ring buffer

```
[host sends OUT packet on EP1]
        │  hardware
        ▼
USB_LP_CAN1_RX0_IRQHandler()                  Core/Src/stm32f1xx_it.c:208
        ▼
HAL_PCD_IRQHandler(&hpcd_USB_FS)              Drivers/…/stm32f1xx_hal_pcd.c
        │  reads ISTR, sees CTR_RX on EP1, USB_ReadPMA() → UserRxBufferFS
        ▼
HAL_PCD_DataOutStageCallback(hpcd, epnum)     USB_DEVICE/Target/usbd_conf.c:130
        ▼
USBD_LL_DataOutStage(pdev, epnum, buf)        Middlewares/…/usbd_core.c:300
        │  epnum != 0 → dispatch to the registered class
        ▼
USBD_CDC_DataOut(pdev, epnum)                 Middlewares/…/usbd_cdc.c:711
        │  hcdc->RxLength = USBD_LL_GetRxDataSize(...)
        ▼
((USBD_CDC_ItfTypeDef*)pdev->pUserData)->Receive(hcdc->RxBuffer, &hcdc->RxLength)
        ▼
CDC_Receive_FS(Buf, Len)   ← YOUR CODE, STILL IN THE ISR
        │  copy bytes into cdc_rx_ring[]
        ▼
returns → ISR returns → CPU resumes wherever the main loop was
```

Later, whenever the main loop gets around to it:

```
App_run() → CDC_ReadChar(&c) → pops one byte from cdc_rx_ring[]
```

### TX: your byte → host terminal

```
App_run() → CDC_Transmit_FS(&c, 1)            (main-loop context)
        │  hcdc->TxState != 0 ? → return USBD_BUSY   [nothing sent!]
        ▼
USBD_CDC_SetTxBuffer(hcdc->TxBuffer = &c, TxLength = 1)
        ▼
USBD_CDC_TransmitPacket()                     usbd_cdc.c
        │  hcdc->TxState = 1        ← "busy" flag raised
        ▼
USBD_LL_Transmit → HAL_PCD_EP_Transmit → USB_EPStartXfer
        │  USB_WritePMA(): copies your bytes into PMA *now*, synchronously
        ▼
CDC_Transmit_FS returns USBD_OK   (the byte is queued in hardware, not yet on the wire)

… microseconds later, host polls IN endpoint, packet goes out …

USB_LP_CAN1_RX0_IRQHandler → HAL_PCD_IRQHandler (CTR_TX on EP1)
        ▼
HAL_PCD_DataInStageCallback → USBD_LL_DataInStage → USBD_CDC_DataIn()
        │  hcdc->TxState = 0       ← released; next Transmit can proceed
```

Two things fall straight out of this diagram:

- **`CDC_Receive_FS` is an ISR callback, not a task.** Every rule of interrupt
  code applies: keep it short, no `HAL_Delay()`, no blocking, no motor moves.
  The existing implementation obeys this — it only copies bytes.
- **`CDC_Transmit_FS` is asynchronous and can silently fail.** It returns
  `USBD_BUSY` if the previous packet hasn't been acknowledged yet, and
  [app_main.c:30](../App/Src/app_main.c#L30) ignores the return value. Type
  fast and you'll lose echoed characters — not received ones.

---

## 6.4 `CDC_Receive_FS()` line by line

```c
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
  for (uint32_t i = 0u; i < *Len; i++) {
    uint16_t next = (uint16_t)((cdc_rx_head + 1u) % CDC_RX_RING_SIZE);
    if (next == cdc_rx_tail) {
      break;                                  // ring full → drop the rest
    }
    cdc_rx_ring[cdc_rx_head] = Buf[i];
    cdc_rx_head = next;
  }

  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
}
```

**Parameters.** `Buf` is `hcdc->RxBuffer`, i.e. `UserRxBufferFS` — the HAL
already copied the packet out of PMA into it. `*Len` is how many bytes arrived
in *this* packet: 1..64 for full speed. A terminal in raw mode sends one byte
per keystroke, so `*Len == 1` most of the time; a paste of 200 characters
arrives as 64 + 64 + 64 + 8 across four separate ISR calls.

**Why a ring buffer at all.** `Buf` is only valid until you return — the next
packet overwrites it. And you cannot do the real work here (a disc move takes
seconds inside an ISR = dead device). So the ISR is the **producer** and
`App_run()` is the **consumer**, decoupled by a 64-byte FIFO.

**Why `volatile`.** `cdc_rx_head`, `cdc_rx_tail` and the array are written by
the ISR and read by the main loop. Without `volatile`, the compiler is entitled
to cache `cdc_rx_head` in a register across the `while(1)` loop and conclude
`tail == head` forever — `CDC_ReadChar()` would never see a byte. This is the
classic "works at -O0, hangs at -O2" bug.

**Why no critical section is needed.** Single producer writes only `head`;
single consumer writes only `tail`. On Cortex-M3 a 16-bit aligned load/store is
a single non-interruptible instruction, so neither side can observe a torn
value. This is the standard SPSC lock-free ring. It would **stop** being safe
the moment a second producer (another ISR) or second consumer appeared.

**The overflow policy.** `if (next == cdc_rx_tail) break;` keeps one slot
permanently empty — that's the price of distinguishing "full" from "empty"
with two indices, so the usable capacity is 63, not 64. New bytes are dropped
rather than overwriting unread ones. That's the right choice for a command
stream (old characters still matter), and given a disc move takes ~seconds,
this buffer fills after ~63 fast keystrokes and then silently loses input.

**The two lines at the end are the important ones.** Read the doc comment ST
left above the function: *"This function will issue a NAK packet on any OUT
packet received on USB endpoint until exiting this function."* While you're
inside `CDC_Receive_FS`, the OUT endpoint is NAK'd — the host retries, which is
how USB applies backpressure for free. `USBD_CDC_ReceivePacket()` calls
`USBD_LL_PrepareReceive(..., CDC_DATA_FS_OUT_PACKET_SIZE)` = re-arms the
endpoint for the next 64 bytes. **Delete that call and you receive exactly one
packet after boot and then go permanently deaf** — the single most common
CubeMX CDC bug. (`USBD_CDC_SetRxBuffer(&Buf[0])` just re-points the class at
the same buffer; it's redundant here but harmless, and it's the hook you'd use
for a double-buffer scheme.)

**Minor nit in the current code:** the comment says the size is a power of two
"so the index wrap is a cheap mask", but the code uses `% CDC_RX_RING_SIZE`.
With a constant 64 the compiler emits `AND #63` anyway, so the comment is
effectively true — but `& (CDC_RX_RING_SIZE - 1u)` would make the intent
explicit and stay correct if someone changes the size to a non-power-of-two.

---

## 6.5 `CDC_ReadChar()` and `App_run()` — the consumer side

```c
uint8_t CDC_ReadChar(uint8_t *out)
{
  if (cdc_rx_tail == cdc_rx_head) return 0u;      // empty
  *out = cdc_rx_ring[cdc_rx_tail];
  cdc_rx_tail = (uint16_t)((cdc_rx_tail + 1u) % CDC_RX_RING_SIZE);
  return 1u;
}
```

Non-blocking by design: it answers "is there a byte?" and "give it to me" in
one call, and `*out` is untouched when it returns 0. The order matters — read
the payload *before* advancing `tail`, otherwise the ISR could refill that slot
between the two operations.

```c
void App_run(void) {
  uint8_t c;
  if (!CDC_ReadChar(&c)) return;                 // nothing pending → back to loop

  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
  CDC_Transmit_FS(&c, 1u);
  translate_char_on_disc(c);
}
```

`App_run()` plays **no part in making USB work.** USB would enumerate, receive,
and even echo (if you called `CDC_Transmit_FS` from the ISR) with `App_run()`
gone entirely. Its job is only to *drain* the FIFO and do the slow work at a
point where blocking is legal.

That said, its timing shapes the *behaviour* you observe:

- `translate_char_on_disc(c)` → `move_to_angle()` blocks for seconds
  (`HAL_Delay()` calls plus a `while (Stepper_IsBusy(...))` spin in
  [motion_planner.c](../App/Src/motion_planner.c#L161)). During that time
  `App_run()` never returns, so **no byte is consumed** — yet USB keeps
  receiving normally in the ISR, filling the ring behind your back. That
  asymmetry (RX never stalls, processing does) is the whole reason the ring
  buffer exists.
- One byte per `App_run()` call. A drain loop
  (`while (CDC_ReadChar(&c)) { … }`) would be wrong here on purpose: you
  *want* one disc move per iteration, not a burst.

---

## 6.6 Why `uint8_t c` is not `static` — the real reason

The instinct "I pass `&c` to a transmit function, so the data must outlive the
call" is exactly the right instinct. It just doesn't apply here, for two
independent reasons:

**1. The byte is already yours before you transmit it.** `CDC_ReadChar(&c)`
*copied* the byte out of `cdc_rx_ring` into `c`. `c` is a private local; no
other code, ISR included, can see or touch it. Each `App_run()` call handles
exactly one byte from start to finish and needs no memory of the previous call
— which is the definition of a stack variable. Making it `static` would gain
nothing and cost you: a `static` would be shared state, and if you ever
restructured toward reentrancy or a second consumer it would become a bug.

**2. `CDC_Transmit_FS()` finishes reading `&c` before it returns — on this
MCU, for this size.** Follow the chain: `USBD_CDC_TransmitPacket()` →
`USBD_LL_Transmit()` → `HAL_PCD_EP_Transmit()` → `USB_EPStartXfer()` →
`USB_WritePMA(USBx, ep->xfer_buff, pmabuffer, len)`. `USB_WritePMA` copies the
bytes into the USB peripheral's packet memory **right there, synchronously**,
before returning. The STM32F103's `USB` IP (not OTG) has no DMA out of user
RAM. By the time `CDC_Transmit_FS` returns, your byte lives in PMA; `c` going
out of scope is irrelevant.

> ⚠️ **Do not generalise this.** Two ways it breaks:
>
> - **Transfers larger than 64 bytes.** For `len > maxpacket` the driver stores
>   your pointer in `ep->xfer_buff`, sends the first 64 bytes, and the
>   *interrupt handler* later advances `ep->xfer_buff += TxPctSize` and calls
>   `USB_WritePMA` again
>   ([stm32f1xx_hal_pcd.c:2435](../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pcd.c#L2435)).
>   That read happens **after** your function returned. So
>   `CDC_Transmit_FS(local_128_byte_buf, 128)` is a genuine use-after-scope
>   bug — the tail of the message comes out as garbage, intermittently.
> - **OTG parts (F4/F7/H7).** There, data is pushed into the TX FIFO from the
>   interrupt handler, so *even a short* transfer reads your buffer after the
>   call returns. Code that is correct here is broken when ported.
>
> **Rule of thumb:** treat `CDC_Transmit_FS` as *asynchronous* and give it a
> `static` or global buffer whenever `Len > 1` or the buffer isn't a
> single scalar. A one-byte echo off the stack is the narrow case that happens
> to be safe.

**What `CDC_Transmit_FS` really guards against.** Not lifetime — concurrency:

```c
USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
if (hcdc->TxState != 0) return USBD_BUSY;    // previous packet still in flight
USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
```

`TxState` is set to 1 by `USBD_CDC_TransmitPacket` and cleared to 0 by
`USBD_CDC_DataIn()` in the ISR when the host has actually collected the packet
(with a zero-length-packet dance first if the length was an exact multiple of
64 — that's what the `total_length % maxpacket == 0` branch in `USBD_CDC_DataIn`
is for). So the endpoint holds **one packet at a time**, and a caller who
doesn't check the return value drops data. Two robustness upgrades, in order of
value:

1. Check the result in `App_run()` (at minimum, don't pretend it succeeded).
2. Mirror the RX design with a TX ring buffer drained by `USBD_CDC_DataIn`.

Also note `hcdc` is dereferenced without a NULL check. `pClassData` is NULL
until the host issues `SET_CONFIGURATION`, so calling `CDC_Transmit_FS()`
before enumeration completes is a hard fault. You are safe today only because
you never transmit unless a byte arrived first, which implies configuration
already happened. If you ever add a boot-time banner message, add the check.

---

## 6.7 "It sleeps and wakes on input" — what's actually happening

It doesn't sleep. There is no `__WFI()`, no `HAL_PWR_EnterSLEEPMode()`, and
`hpcd_USB_FS.Init.low_power_enable = DISABLE` in `USBD_LL_Init()`. The core
runs flat out at 48 MHz:

```
while (1) { App_run(); }        // idle path: CDC_ReadChar() → 0 → return. Repeat.
                                // millions of times per second, LED off, nothing visible.
```

What you're seeing is **poll-and-return**, which is behaviourally
indistinguishable from sleep-and-wake from the outside. The real sequence on a
keystroke is: host sends a packet → hardware raises the USB interrupt → ISR
queues the byte → *the loop, which was already running,* observes a non-empty
ring on its next pass and reacts within microseconds.

If you want it to *really* sleep, the pieces are already in place — the USB
interrupt is what wakes the core out of `WFI`:

```c
while (1) {
  App_run();
  __WFI();          // sleep until ANY interrupt (USB, SysTick, TIM2/3, USART)
}
```

Caveat: SysTick fires every 1 ms and will wake you constantly, so `WFI` alone
buys little until you also suppress SysTick during idle. Note too that
`HAL_Delay()` inside the motion planner is a busy-wait on SysTick, so the
"real" power hog is the motion path, not the idle path. The suspend/resume
callbacks (`PCD_SuspendCallback` / `PCD_ResumeCallback` in `usbd_conf.c`) are
where you'd hook proper USB-suspend low-power behaviour — they're generated but
effectively empty because `low_power_enable` is `DISABLE`.

---

## 6.8 Reference: who calls whom (one table)

| Function | File | Context | Called by |
|---|---|---|---|
| `MX_USB_DEVICE_Init` | `USB_DEVICE/App/usb_device.c` | main, once | `main()` |
| `USBD_LL_Init` | `USB_DEVICE/Target/usbd_conf.c` | main, once | `USBD_Init` |
| `HAL_PCD_MspInit` | `USB_DEVICE/Target/usbd_conf.c` | main, once | `HAL_PCD_Init` — **arms the NVIC line** |
| `USB_LP_CAN1_RX0_IRQHandler` | `Core/Src/stm32f1xx_it.c` | **ISR prio 0** | hardware |
| `HAL_PCD_IRQHandler` | `Drivers/…/stm32f1xx_hal_pcd.c` | **ISR** | the handler above |
| `CDC_Init_FS` | `USB_DEVICE/App/usbd_cdc_if.c` | **ISR** (at SET_CONFIGURATION) | `USBD_CDC_Init` |
| `CDC_Control_FS` | `USB_DEVICE/App/usbd_cdc_if.c` | **ISR** | `USBD_CDC_Setup` (baud-rate etc. — all no-ops here, correctly: baud is meaningless for a virtual port) |
| `CDC_Receive_FS` | `USB_DEVICE/App/usbd_cdc_if.c` | **ISR** | `USBD_CDC_DataOut` via `pUserData->Receive` |
| `USBD_CDC_DataIn` | `Middlewares/…/usbd_cdc.c` | **ISR** | `USBD_LL_DataInStage` — clears `TxState` |
| `CDC_ReadChar` | `USB_DEVICE/App/usbd_cdc_if.c` | main loop | `App_run` |
| `CDC_Transmit_FS` | `USB_DEVICE/App/usbd_cdc_if.c` | main loop | `App_run` |

Shared state between the two contexts, in full: `cdc_rx_ring` / `cdc_rx_head` /
`cdc_rx_tail` (yours, `volatile`, SPSC-safe) and `hcdc->TxState` (ST's, read in
main context, written in ISR — benign here because a stale "busy" read only
delays a packet).

---

## 6.9 Things worth fixing / trying next

Ordered by how much they matter:

1. **Check the `CDC_Transmit_FS` return value** in `App_run()`. Today a busy
   endpoint silently swallows the echo.
2. **Commit `Middlewares/` and `USB_DEVICE/`.** They're untracked; the build
   depends on them.
3. **Update the 32 MHz claim** in `02-execution-flow.md` and re-check any timer
   math derived from it — the clock is 48 MHz now.
4. **Reclaim ~1 KB of RAM** by shrinking `APP_TX_DATA_SIZE`, since
   `UserTxBufferFS` is never used. (Keep `APP_RX_DATA_SIZE` ≥ 64.)
5. **Add a NULL check on `pClassData`** in `CDC_Transmit_FS` before you ever
   transmit un-prompted.
6. **Backpressure:** with ~seconds per disc move, decide what "too fast" means
   — drop (today), or send an XOFF / "busy" byte back to the host when the ring
   is over half full.
7. **Experiment to confirm the ISR/loop split for yourself:** put a
   `HAL_Delay(3000)` at the top of `App_run()` and hold a key down. The
   characters still get received (the ring fills) and echo out late in a burst —
   proof that reception doesn't depend on the loop.

---

**Previous:** [05-is-it-overengineered.md](05-is-it-overengineered.md) ·
**Index:** [README.md](README.md)
