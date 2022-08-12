# CoolStar Audio for Intel Haswell / Broadwell SST

Open Source alternative for the Intel Haswell / Broadwell SST driver (untested on Haswell)

Tested on Google Pixel 2 LS (Core i7-5500U)

Based off csaudioacp3x

catpt commit: b695f5c0a86ea685500a72b6a9959da041f26da6
dma/dw commit: d5a8fe0fee54d830c47959f625ffc41d080ee526

# Binary Blob

This driver requires a binary blob you can get here: https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/intel/IntcSST2.bin

Drop this binary into Source/Main and you can build

Known working SHA256: 78DC1771E6AE22A392CC9E249AE2817B01EC0D3CABCF631719624CDD3CF5AB81