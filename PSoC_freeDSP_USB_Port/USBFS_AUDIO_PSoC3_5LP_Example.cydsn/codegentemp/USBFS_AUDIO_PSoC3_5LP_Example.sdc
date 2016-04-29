# THIS FILE IS AUTOMATICALLY GENERATED
# Project: C:\Users\Lukas Creutzburg\Documents\GitHub\PSoC5LP-freeDSP-USB-Port\PSoC_freeDSP_USB_Port\USBFS_AUDIO_PSoC3_5LP_Example.cydsn\USBFS_AUDIO_PSoC3_5LP_Example.cyprj
# Date: Fri, 29 Apr 2016 12:23:32 GMT
#set_units -time ns
create_clock -name {SampleClk(routed)} -period 31250 -waveform {0 15625} [list [get_pins {ClockBlock/dclk_0}]]
create_clock -name {CyILO} -period 10000 -waveform {0 5000} [list [get_pins {ClockBlock/ilo}] [get_pins {ClockBlock/clk_100k}] [get_pins {ClockBlock/clk_1k}] [get_pins {ClockBlock/clk_32k}]]
create_clock -name {CyIMO} -period 41.666666666666664 -waveform {0 20.8333333333333} [list [get_pins {ClockBlock/imo}]]
create_clock -name {CyPLL_OUT} -period 15.625 -waveform {0 7.8125} [list [get_pins {ClockBlock/pllout}]]
create_clock -name {CyMASTER_CLK} -period 15.625 -waveform {0 7.8125} [list [get_pins {ClockBlock/clk_sync}]]
create_generated_clock -name {CyBUS_CLK} -source [get_pins {ClockBlock/clk_sync}] -edges {1 2 3} [list [get_pins {ClockBlock/clk_bus_glb}]]
create_generated_clock -name {SampleClk} -source [get_pins {ClockBlock/clk_sync}] -edges {1 2001 4001} [list [get_pins {ClockBlock/dclk_glb_0}]]


# Component constraints for C:\Users\Lukas Creutzburg\Documents\GitHub\PSoC5LP-freeDSP-USB-Port\PSoC_freeDSP_USB_Port\USBFS_AUDIO_PSoC3_5LP_Example.cydsn\TopDesign\TopDesign.cysch
# Project: C:\Users\Lukas Creutzburg\Documents\GitHub\PSoC5LP-freeDSP-USB-Port\PSoC_freeDSP_USB_Port\USBFS_AUDIO_PSoC3_5LP_Example.cydsn\USBFS_AUDIO_PSoC3_5LP_Example.cyprj
# Date: Fri, 29 Apr 2016 12:23:28 GMT
