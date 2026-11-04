<?xml version="1.0" encoding="UTF-8"?>
<Project Version="3" Minor="2" Path="C:/igw_r15/IGW_GATEWAY_R15_CORE/project_r15/soc_prj">
    <Project_Created_Time></Project_Created_Time>
    <TD_Encoding>UTF-8</TD_Encoding>
    <TD_Version>6.2.200067</TD_Version>
    <Name>soc_prj</Name>
    <HardWare>
        <Family>DR1</Family>
        <Device>DR1M90GEG400</Device>
        <Speed>-2</Speed>
    </HardWare>
    <Source_Files>
        <Verilog>
            <File Path="src/system_top.v"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="1"/></FileInfo></File>
            <File Path="design_0/hdl/design_0.v"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="2"/></FileInfo></File>
            <File Path="src/igw/igw_axi_reject.v"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="3"/></FileInfo></File>
            <File Path="src/igw/igw_can_observer_axi.v"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="4"/></FileInfo></File>
            <File Path="src/igw/igw_can_rx_observer.v"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="5"/></FileInfo></File>
            <File Path="src/igw/igw_rs485_axi.v"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="6"/></FileInfo></File>
            <File Path="src/igw/igw_rs485_branch.v"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="7"/></FileInfo></File>
            <File Path="src/igw/igw_rs485_tx.v"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="8"/></FileInfo></File>
            <File Path="src/igw/igw_rtu_mailbox.v"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="9"/></FileInfo></File>
            <File Path="src/igw/igw_uart.v"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="10"/></FileInfo></File>
        </Verilog>
        <ADC_FILE>
            <File Path="src/constraint/io.adc"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="constraint_1"/><Attr Name="CompileOrder" Val="1"/></FileInfo></File>
        </ADC_FILE>
        <SDC_FILE>
            <File Path="src/constraint/timing.sdc"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="constraint_1"/><Attr Name="CompileOrder" Val="1"/></FileInfo></File>
        </SDC_FILE>
        <IP_FILE>
            <File Path="design_0/ip/design_0_ARM_Processor_System_0/design_0_ARM_Processor_System_0.xml"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="1"/></FileInfo></File>
            <File Path="design_0/ip/design_0_AXI_Matrix_1/design_0_AXI_Matrix_1.xml"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="2"/></FileInfo></File>
            <File Path="design_0/ip/AXI_Protocol_Converter_0/AXI_Protocol_Converter_0.xml"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="3"/></FileInfo></File>
            <File Path="design_0/ip/design_0_AXI_Protocol_Converter_1/design_0_AXI_Protocol_Converter_1.xml"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="4"/></FileInfo></File>
            <File Path="design_0/ip/design_0_Concat_0/design_0_Concat_0.xml"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="5"/></FileInfo></File>
            <File Path="design_0/ip/design_0_PLL_1/design_0_PLL_1.xml"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="6"/></FileInfo></File>
            <File Path="design_0/ip/design_0_Video_NPU_0/design_0_Video_NPU_0.xml"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="7"/></FileInfo></File>
            <File Path="design_0/ip/design_0_igw_rs485_branch_0/design_0_igw_rs485_branch_0.xml"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="8"/></FileInfo></File>
            <File Path="design_0/ip/design_0_PLL_2/design_0_PLL_2.xml"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="9"/></FileInfo></File>
            <File Path="design_0/ip/design_0_PLL_0/design_0_PLL_0.xml"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="10"/></FileInfo></File>
            <File Path="design_0/ip/design_0_Processor_System_Reset_0/design_0_Processor_System_Reset_0.xml"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="11"/></FileInfo></File>
            <File Path="design_0/ip/design_0_Processor_System_Reset_1/design_0_Processor_System_Reset_1.xml"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="12"/></FileInfo></File>
            <File Path="design_0/ip/design_0_Processor_System_Reset_2/design_0_Processor_System_Reset_2.xml"><FileInfo><Attr Name="UsedInSyn" Val="true"/><Attr Name="UsedInP&R" Val="true"/><Attr Name="BelongTo" Val="design_1"/><Attr Name="CompileOrder" Val="13"/></FileInfo></File>
        </IP_FILE>
    </Source_Files>
    <FileSets>
        <FileSet Name="design_1" Type="DesignFiles">
        </FileSet>
        <FileSet Name="constraint_1" Type="ConstrainFiles">
        </FileSet>
    </FileSets>
    <TOP_MODULE>
        <LABEL>system_top</LABEL>
        <MODULE>system_top</MODULE>
        <CREATEINDEX>user</CREATEINDEX>
    </TOP_MODULE>
    <Property>
    </Property>
    <Device_Settings>
    </Device_Settings>
    <Configurations>
    </Configurations>
    <Runs>
        <Run Name="syn_1" Type="Synthesis" ConstraintSet="constraint_1" Description="" Active="true">
            <Strategy Name="Default_Synthesis_Strategy">
            </Strategy>
            <UserParams>
            </UserParams>
        </Run>
        <Run Name="phy_1" Type="PhysicalDesign" ConstraintSet="constraint_1" Description="" SynRun="syn_1" Active="true">
            <Strategy Name="Default_PhysicalDesign_Strategy">
                <BitgenProperty::GeneralOption>
                    <compress>on</compress>
                </BitgenProperty::GeneralOption>
                <PlaceProperty>
                    <opt_timing>high</opt_timing>
                </PlaceProperty>
            </Strategy>
            <UserParams>
            </UserParams>
        </Run>
    </Runs>
    <Project_Settings>
    </Project_Settings>
</Project>
