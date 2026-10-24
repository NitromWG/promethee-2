<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE eagle SYSTEM "eagle.dtd">
<eagle version="9.6.2">
<drawing>
<settings>
<setting alwaysvectorfont="no"/>
</settings>
<grid distance="0.1" unitdist="inch" unit="inch" style="lines" multiple="1" display="no" altdistance="0.01" altunitdist="inch" altunit="inch"/>
<layers>
<layer number="1" name="Top" color="4" fill="1" visible="yes" active="yes"/>
<layer number="16" name="Bottom" color="1" fill="1" visible="yes" active="yes"/>
<layer number="17" name="Pads" color="2" fill="1" visible="yes" active="yes"/>
<layer number="18" name="Vias" color="2" fill="1" visible="yes" active="yes"/>
<layer number="20" name="Dimension" color="15" fill="1" visible="yes" active="yes"/>
<layer number="21" name="tPlace" color="7" fill="1" visible="yes" active="yes"/>
<layer number="25" name="tNames" color="7" fill="1" visible="yes" active="yes"/>
<layer number="35" name="tGlue" color="7" fill="1" visible="yes" active="yes"/>
<layer number="39" name="tKeepout" color="4" fill="11" visible="yes" active="yes"/>
<layer number="51" name="tDocu" color="7" fill="1" visible="yes" active="yes"/>
<layer number="91" name="Nets" color="2" fill="1" visible="yes" active="yes"/>
<layer number="94" name="Symbols" color="4" fill="1" visible="yes" active="yes"/>
<layer number="95" name="Names" color="7" fill="1" visible="yes" active="yes"/>
<layer number="96" name="Values" color="7" fill="1" visible="yes" active="yes"/>
</layers>
<schematic xreflabel="%F%N/%S.%C%R" xrefpart="/%S.%C%R">
<libraries>
<library name="rcl">
<packages>
<package name="R0603">
<description>Résistance CMS 0603</description>
<wire x1="-0.432" y1="-0.356" x2="0.432" y2="-0.356" width="0.1524" layer="51"/>
<wire x1="0.432" y1="0.356" x2="-0.432" y2="0.356" width="0.1524" layer="51"/>
<wire x1="-1.473" y1="0.983" x2="1.473" y2="0.983" width="0.0508" layer="39"/>
<smd name="1" x="-0.8" y="0" dx="0.9" dy="0.95" layer="1" roundness="25"/>
<smd name="2" x="0.8" y="0" dx="0.9" dy="0.95" layer="1" roundness="25"/>
<text x="-0.889" y="0.889" size="1.27" layer="25">&gt;NAME</text>
<rectangle x1="-0.1999" y1="-0.3" x2="0.1999" y2="0.3" layer="35"/>
</package>
</packages>
<symbols>
<symbol name="R-EU">
<wire x1="-2.54" y1="-0.889" x2="2.54" y2="-0.889" width="0.254" layer="94"/>
<wire x1="2.54" y1="0.889" x2="-2.54" y2="0.889" width="0.254" layer="94"/>
<wire x1="2.54" y1="-0.889" x2="2.54" y2="0.889" width="0.254" layer="94"/>
<wire x1="-2.54" y1="-0.889" x2="-2.54" y2="0.889" width="0.254" layer="94"/>
<text x="-3.81" y="1.4986" size="1.778" layer="95">&gt;NAME</text>
<text x="-3.81" y="-3.302" size="1.778" layer="96">&gt;VALUE</text>
<pin name="2" x="5.08" y="0" visible="off" length="short" direction="pas" swaplevel="1" rot="R180"/>
<pin name="1" x="-5.08" y="0" visible="off" length="short" direction="pas" swaplevel="1"/>
</symbol>
</symbols>
<devicesets>
<deviceset name="R-EU_" prefix="R" uservalue="yes">
<description>&lt;b&gt;RESISTOR&lt;/b&gt;, European symbol</description>
<gates>
<gate name="G$1" symbol="R-EU" x="0" y="0"/>
</gates>
<devices>
<device name="R0603" package="R0603">
<connects>
<connect gate="G$1" pin="1" pad="1"/>
<connect gate="G$1" pin="2" pad="2"/>
</connects>
<technologies>
<technology name=""/>
</technologies>
</device>
</devices>
</deviceset>
</devicesets>
</library>
<library name="linear">
<packages>
<package name="DIL08">
<description>Boîtier DIL 8 broches</description>
<wire x1="5.08" y1="2.921" x2="-5.08" y2="2.921" width="0.1524" layer="21"/>
<wire x1="-5.08" y1="-2.921" x2="5.08" y2="-2.921" width="0.1524" layer="21"/>
<wire x1="5.08" y1="2.921" x2="5.08" y2="-2.921" width="0.1524" layer="21"/>
<wire x1="-5.08" y1="2.921" x2="-5.08" y2="1.016" width="0.1524" layer="21"/>
<wire x1="-5.08" y1="-2.921" x2="-5.08" y2="-1.016" width="0.1524" layer="21"/>
<wire x1="-5.08" y1="1.016" x2="-5.08" y2="-1.016" width="0.1524" layer="21" curve="-180"/>
<pad name="1" x="-3.81" y="-3.81" drill="0.8128" shape="long" rot="R90"/>
<pad name="2" x="-1.27" y="-3.81" drill="0.8128" shape="long" rot="R90"/>
<pad name="7" x="-1.27" y="3.81" drill="0.8128" shape="long" rot="R90"/>
<pad name="8" x="-3.81" y="3.81" drill="0.8128" shape="long" rot="R90"/>
<pad name="3" x="1.27" y="-3.81" drill="0.8128" shape="long" rot="R90"/>
<pad name="4" x="3.81" y="-3.81" drill="0.8128" shape="long" rot="R90"/>
<pad name="6" x="1.27" y="3.81" drill="0.8128" shape="long" rot="R90"/>
<pad name="5" x="3.81" y="3.81" drill="0.8128" shape="long" rot="R90"/>
<text x="-5.334" y="-2.921" size="1.27" layer="25" rot="R90">&gt;NAME</text>
</package>
</packages>
<symbols>
<symbol name="OPAMP">
<wire x1="-5.08" y1="5.08" x2="-5.08" y2="-5.08" width="0.4064" layer="94"/>
<wire x1="-5.08" y1="-5.08" x2="5.08" y2="0" width="0.4064" layer="94"/>
<wire x1="5.08" y1="0" x2="-5.08" y2="5.08" width="0.4064" layer="94"/>
<text x="2.54" y="5.715" size="1.778" layer="95">&gt;NAME</text>
<pin name="-IN" x="-7.62" y="-2.54" visible="pad" length="short" direction="in"/>
<pin name="+IN" x="-7.62" y="2.54" visible="pad" length="short" direction="in"/>
<pin name="OUT" x="7.62" y="0" visible="pad" length="short" direction="out" rot="R180"/>
</symbol>
<symbol name="PWR+-">
<text x="1.27" y="3.175" size="0.8128" layer="93" rot="R90">V+</text>
<text x="1.27" y="-4.445" size="0.8128" layer="93" rot="R90">V-</text>
<pin name="V+" x="0" y="7.62" visible="pad" length="middle" direction="pwr" rot="R270"/>
<pin name="V-" x="0" y="-7.62" visible="pad" length="middle" direction="pwr" rot="R90"/>
</symbol>
</symbols>
<devicesets>
<deviceset name="LM358" prefix="IC">
<description>Double amplificateur opérationnel</description>
<gates>
<gate name="A" symbol="OPAMP" x="0" y="15.24" swaplevel="1"/>
<gate name="B" symbol="OPAMP" x="0" y="0" swaplevel="1"/>
<gate name="P" symbol="PWR+-" x="0" y="0" addlevel="request"/>
</gates>
<devices>
<device name="N" package="DIL08">
<connects>
<connect gate="A" pin="+IN" pad="3"/>
<connect gate="A" pin="-IN" pad="2"/>
<connect gate="A" pin="OUT" pad="1"/>
<connect gate="B" pin="+IN" pad="5"/>
<connect gate="B" pin="-IN" pad="6"/>
<connect gate="B" pin="OUT" pad="7"/>
<connect gate="P" pin="V+" pad="8"/>
<connect gate="P" pin="V-" pad="4"/>
</connects>
<technologies>
<technology name=""/>
</technologies>
</device>
</devices>
</deviceset>
</devicesets>
</library>
<library name="supply1">
<symbols>
<symbol name="GND">
<wire x1="-1.905" y1="0" x2="1.905" y2="0" width="0.254" layer="94"/>
<text x="-2.54" y="-2.54" size="1.778" layer="96">&gt;VALUE</text>
<pin name="GND" x="0" y="2.54" visible="off" length="short" direction="sup" rot="R270"/>
</symbol>
<symbol name="VCC">
<wire x1="1.27" y1="-1.905" x2="0" y2="0" width="0.254" layer="94"/>
<wire x1="0" y1="0" x2="-1.27" y2="-1.905" width="0.254" layer="94"/>
<text x="-2.54" y="-2.54" size="1.778" layer="96" rot="R90">&gt;VALUE</text>
<pin name="VCC" x="0" y="-2.54" visible="off" length="short" direction="sup" rot="R90"/>
</symbol>
</symbols>
<devicesets>
<deviceset name="GND" prefix="GND">
<gates>
<gate name="1" symbol="GND" x="0" y="0"/>
</gates>
<devices>
<device name="">
<technologies>
<technology name=""/>
</technologies>
</device>
</devices>
</deviceset>
<deviceset name="VCC" prefix="P+">
<gates>
<gate name="VCC" symbol="VCC" x="0" y="0"/>
</gates>
<devices>
<device name="">
<technologies>
<technology name=""/>
</technologies>
</device>
</devices>
</deviceset>
</devicesets>
</library>
</libraries>
<parts>
<part name="IC1" library="linear" deviceset="LM358" device="N" value="LM358N"/>
<part name="R1" library="rcl" deviceset="R-EU_" device="R0603" value="10k"/>
<part name="R2" library="rcl" deviceset="R-EU_" device="R0603" value="47k"/>
<part name="R3" library="rcl" deviceset="R-EU_" device="R0603" value="1k">
<attribute name="MPN" value="RC0603FR-071KL"/>
</part>
<part name="R4" library="rcl" deviceset="R-EU_" device="R0603" value="4k7"/>
<part name="GND1" library="supply1" deviceset="GND" device=""/>
<part name="GND2" library="supply1" deviceset="GND" device=""/>
<part name="GND3" library="supply1" deviceset="GND" device=""/>
<part name="P+1" library="supply1" deviceset="VCC" device=""/>
</parts>
<sheets>
<sheet>
<plain>
<text x="20.32" y="76.2" size="2.54" layer="97">Étage d'entrée</text>
</plain>
<instances>
<instance part="IC1" gate="A" x="50.8" y="50.8"/>
<instance part="R1" gate="G$1" x="30.48" y="53.34" rot="MR0"/>
<instance part="R2" gate="G$1" x="50.8" y="40.64"/>
<instance part="R4" gate="G$1" x="40.64" y="33.02" rot="R90"/>
<instance part="GND1" gate="1" x="40.64" y="22.86"/>
</instances>
<busses>
</busses>
<nets>
<net name="IN" class="0">
<segment>
<pinref part="R1" gate="G$1" pin="2"/>
<wire x1="25.4" y1="53.34" x2="20.32" y2="53.34" width="0.1524" layer="91"/>
<label x="20.32" y="53.34" size="1.778" layer="95" rot="R180" xref="yes"/>
</segment>
</net>
<net name="N$1" class="0">
<segment>
<pinref part="R1" gate="G$1" pin="1"/>
<pinref part="IC1" gate="A" pin="+IN"/>
<wire x1="35.56" y1="53.34" x2="43.18" y2="53.34" width="0.1524" layer="91"/>
</segment>
</net>
<net name="N$2" class="0">
<segment>
<pinref part="IC1" gate="A" pin="-IN"/>
<pinref part="R2" gate="G$1" pin="1"/>
<pinref part="R4" gate="G$1" pin="2"/>
<wire x1="43.18" y1="48.26" x2="40.64" y2="48.26" width="0.1524" layer="91"/>
<wire x1="40.64" y1="48.26" x2="40.64" y2="40.64" width="0.1524" layer="91"/>
<wire x1="40.64" y1="40.64" x2="40.64" y2="38.1" width="0.1524" layer="91"/>
<wire x1="40.64" y1="40.64" x2="45.72" y2="40.64" width="0.1524" layer="91"/>
<junction x="40.64" y="40.64"/>
</segment>
</net>
<net name="OUT" class="0">
<segment>
<pinref part="R2" gate="G$1" pin="2"/>
<pinref part="IC1" gate="A" pin="OUT"/>
<wire x1="55.88" y1="40.64" x2="60.96" y2="40.64" width="0.1524" layer="91"/>
<wire x1="60.96" y1="40.64" x2="60.96" y2="50.8" width="0.1524" layer="91"/>
<wire x1="58.42" y1="50.8" x2="60.96" y2="50.8" width="0.1524" layer="91"/>
<wire x1="60.96" y1="50.8" x2="66.04" y2="50.8" width="0.1524" layer="91"/>
<junction x="60.96" y="50.8"/>
<label x="66.04" y="50.8" size="1.778" layer="95"/>
</segment>
</net>
<net name="GND" class="0">
<segment>
<pinref part="R4" gate="G$1" pin="1"/>
<pinref part="GND1" gate="1" pin="GND"/>
<wire x1="40.64" y1="27.94" x2="40.64" y2="25.4" width="0.1524" layer="91"/>
</segment>
</net>
</nets>
</sheet>
<sheet>
<plain>
</plain>
<instances>
<instance part="IC1" gate="P" x="50.8" y="50.8"/>
<instance part="P+1" gate="VCC" x="50.8" y="66.04"/>
<instance part="GND2" gate="1" x="50.8" y="35.56"/>
<instance part="R3" gate="G$1" x="76.2" y="50.8" rot="R270"/>
<instance part="GND3" gate="1" x="76.2" y="40.64"/>
</instances>
<busses>
</busses>
<nets>
<net name="VCC" class="0">
<segment>
<pinref part="IC1" gate="P" pin="V+"/>
<pinref part="P+1" gate="VCC" pin="VCC"/>
<wire x1="50.8" y1="58.42" x2="50.8" y2="63.5" width="0.1524" layer="91"/>
</segment>
</net>
<net name="GND" class="0">
<segment>
<pinref part="IC1" gate="P" pin="V-"/>
<pinref part="GND2" gate="1" pin="GND"/>
<wire x1="50.8" y1="43.18" x2="50.8" y2="38.1" width="0.1524" layer="91"/>
</segment>
<segment>
<pinref part="R3" gate="G$1" pin="2"/>
<pinref part="GND3" gate="1" pin="GND"/>
<wire x1="76.2" y1="45.72" x2="76.2" y2="43.18" width="0.1524" layer="91"/>
</segment>
</net>
<net name="OUT" class="0">
<segment>
<pinref part="R3" gate="G$1" pin="1"/>
<wire x1="76.2" y1="55.88" x2="76.2" y2="60.96" width="0.1524" layer="91"/>
<wire x1="76.2" y1="60.96" x2="71.12" y2="60.96" width="0.1524" layer="91"/>
<label x="71.12" y="60.96" size="1.778" layer="95" rot="R180"/>
</segment>
</net>
</nets>
</sheet>
</sheets>
</schematic>
</drawing>
</eagle>
