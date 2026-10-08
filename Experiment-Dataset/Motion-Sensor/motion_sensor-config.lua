--[[
This is a bare minimum S2E config file to demonstrate the use of libs2e with PyKVM.
Please refer to the S2E documentation for more details.
This file was automatically generated at 2023-07-30 19:08:13.178009
]]--

s2e = {
    logging = {
        -- Possible values include "all", "debug", "info", "warn" and "none".
        -- See Logging.h in libs2ecore.
        console = "info",
        logLevel = "info",
    },
    -- All the cl::opt options defined in the engine can be tweaked here.
    -- This can be left empty most of the time.
    -- Most of the options can be found in S2EExecutor.cpp and Executor.cpp.
    kleeArgs = {
		"--verbose-on-symbolic-address=true",
		"--verbose-state-switching=true",
		"--verbose-fork-info=true",
		"--print-mode-switch=false",
		"--fork-on-symbolic-address=false",--no self-modifying code and load libs for IoT firmware
		"--suppress-external-warnings=true"
    },
}

--rom start should be equal to vtor
mem = {
	rom = {
		 {0x202000,0x8e000},
	},
	ram = {
		 {0x20000000,0x40000},
	},
}

init = {
   vtor = 2105344,
}

analysis = false

-- Declare empty plugin settings. They will be populated in the rest of
-- the configuration file.
plugins = {}
pluginsConfig = {}

-- Include various convenient functions
dofile('library.lua')




add_plugin("InVulAna")
pluginsConfig.InVulAna = {
	packet_mode = false,
	symbfile = "motion_sensor.symbs",
	inputfile = "motion_sensor.in",
	snapshot_mode = true,
	snapshotfile = "motion_sensor.snapshot",
	-- hook read addresses
	hook_read = { },
	exit_pcs = {},
	-- ram space
	ram = {  0x20000000,0x40000, },
	-- skip read
	skip_read_pc = {  0x20262c,  0x202630,  0x202632,  0x20263e, },
	skip_read_addr = {  0x400d5040,  0x400d5044,  0x400d5048,  0x400d504c, },
	-- skip pc
 	skip = {  0x208d68,  0x2036bc,  0x2053f8, },
	-- force jump from pc in 'jump' to pc in 'to'
	-- up and down is a pair
	jump = {  0x208dd4, },
	to   = {  0x20353c, },
	-- the pc where isr need to be triggered, mostly idle
	idle = 0x0,
	-- isr numbers which is enabled
	isrs = {  0x8d, },
	user_addrs = {  0x40088828, },
	dma_reg = 0x3fffffff,
	dma_len = 0,
	-- only provide the user input
	user_only_mode = true,
	-- pc where infer mode start
	infer_start = 0x208dd4,
	debug = true,
	-- ghidra server port
	ghidra_port = 22733,
	-- null end
	null_end = 0x10,
	-- instruction based trace
	fidelity = false,
	random = false,
}