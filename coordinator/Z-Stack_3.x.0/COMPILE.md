# Compiling the firmware

This guide describes how to compile both the router and coordinator firmware.

## Setup development environment

1. Download and install [SIMPLELINK-LOWPOWER-F2-SDK 8.30.01.01](https://www.ti.com/tool/download/SIMPLELINK-LOWPOWER-F2-SDK/8.30.01.01)
1. Download and install [Code Composer Studio 20.1.0](https://www.ti.com/tool/download/CCSTUDIO/20.1.0)

## Compiling

1. Create a folder called `workspace` in the folder where the SDK is installed. In the SDK installation folder you should see files like `Makefile` and `license_simplelink_cc13xx_cc26xx_sdk_8_30_01_01.txt`.
1. Start Code Composer Studio, it will ask you to select a workspace folder, select the `workspace` folder you created in the previous step.
1. Go to _File -> Import Projects -> Browse_ and select: `simplelink_cc13xx_cc26xx_sdk_8_30_01_01/examples/rtos`.
1. Select:
   - `znp_CC1352P_2_LAUNCHXL_tirtos7_ticlang`
   - `znp_CC26X2R1_LAUNCHXL_tirtos7_ticlang`
   - `znp_LP_CC1352P7_4_tirtos7_ticlang`
   - `znp_LP_CC2652R7_tirtos7_ticlang`
   - `znp_LP_CC2652RB_tirtos7_ticlang`
   - `znp_LP_EM_CC2674P10_tirtos7_ticlang`
   - `zr_genericapp_CC1352P_2_LAUNCHXL_tirtos7_ticlang`
   - `zr_genericapp_CC26X2R1_LAUNCHXL_tirtos7_ticlang`
   - `zr_genericapp_LP_CC1352P7_4_tirtos7_ticlang`
   - `zr_genericapp_LP_CC2652R7_tirtos7_ticlang`
   - `zr_genericapp_LP_CC2652RB_tirtos7_ticlang`
   - `zr_genericapp_LP_EM_CC2674P10_tirtos7_ticlang`
1. Press _Finish_.
1. In Code Composer Studio, expand the projects and for each open `znp.syscfg` or `zr_genericapp.syscfg`, expand `Power Management` and change `Minimal Poll Period (ms)` to `1000`, change it back to `100` immediately and save the file.
1. Copy `*.patch` to the SDK installation folder, open a Git Bash in this folder and apply the patch using `git apply *.patch --ignore-space-change`.
1. Optional: to build MT frame forwarding support for P7 ZNP, set `ZNP_BACKHAUL_ENABLE` to `1` in `source/preinclude.h`. Leave it at `0` for the ordinary firmware. This setting only affects `znp_LP_CC1352P7_4_tirtos7_ticlang`; the other projects keep their usual configuration. See [BACKHAUL.md](BACKHAUL.md) for the MT interface.
1. When changing this option, run _Project_ -> _Clean_ before rebuilding. Both coordinator and satellite use the optional ZNP image; the satellite is configured as a router by its host.
1. Build the projects; click _Project_ -> _Build all_.
   - **Important:** by default the **launchpad** variant of the CC1352P2_CC2652P (= `CC1352P_2_LAUNCHXL_tirtos7_ticlang`) is build. To build the **other** variant change `#define LAUNCHPAD_CONFIG 1` to `#define LAUNCHPAD_CONFIG 0` in `preinclude.h`.
1. Once finished, the coordinator firmwares can be found under `znp_*_tirtos7_ticlang/default/znp_*_tirtos7_ticlang.hex` and router firmwares under `zr_genericapp_*_tirtos7_ticlang/default/zr_genericapp_*_tirtos7_ticlang.hex`:
   - `*_CC1352P_2_LAUNCHXL_tirtos7_ticlang.hex` -> CC1352P-2 and CC2652P based boards
   - `*_CC26X2R1_LAUNCHXL_tirtos7_ticlang.hex` -> CC2652R based boards
   - `*_LP_CC1352P7_4_tirtos7_ticlang.hex` -> CC1352P7 based boards
   - `*_LP_CC2652R7_tirtos7_ticlang.hex` -> CC2652R7 based boards
   - `*_LP_CC2652RB_tirtos7_ticlang.hex` -> CC2652RB based boards
1. To package all the firmwares, execute `python3 package.py` in the SDK folder.

The optional image is packaged by the same `package.py` as
`CC1352P7_znp_backhaul_<revision>.zip`; ordinary archive names remain unchanged.
Keep the generated `.map` beside the P7 `.hex`: packaging identifies the variant
and revision from the compiled output, even if `preinclude.h` was changed later.

## Radio regression tests

From `coordinator/Z-Stack_3.x.0`, run:

```sh
python3 tests/test_backhaul.py
python3 tests/test_backhaul.py --sdk /path/to/simplelink_cc13xx_cc26xx_sdk_8_30_01_01
```

The first command requires Git, Python 3 and GCC on Linux; it reads production
sources from `firmware.patch` and needs no SDK or hardware. The second also tests
the patched SDK callback and real OSAL/NVOCMP storage. It requires the same SDK
workspace prepared above. Both commands use AddressSanitizer and
UndefinedBehaviorSanitizer and leave only temporary build files.
