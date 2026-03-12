# HWPE Designs

This directory contains custom Hardware Processing Engine (HWPE) accelerator designs for the PULP platform.

## Structure

Each HWPE gets its own subdirectory:

```
hwpe/
└── <your-hwpe>/
    ├── src/           # C test programs and HAL drivers
    ├── model/         # GVSoC C++ simulation model (optional)
    └── Makefile
```

## Creating a New HWPE

1. Create a subdirectory here (e.g. `hwpe/my-accelerator/`)
2. Write a HAL header (`hal_*.h`) with register definitions and control macros
3. Write a C test program that exercises the accelerator through the HAL
4. (Optional) Write a GVSoC model so the simulator can execute your design
5. Build and run: `source setup_env.sh && cd hwpe/<name> && make clean all run`

Use `pulp-sdk/tests/` examples as reference for Makefile structure.
