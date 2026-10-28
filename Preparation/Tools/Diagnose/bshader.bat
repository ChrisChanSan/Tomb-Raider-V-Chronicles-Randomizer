@echo off
rem bshader - compile the HD shaders with fxc and write the .inc byte arrays.
rem
rem The engine has no runtime shader compiler and no d3dx, so the bytecode is
rem checked in next to its HLSL source (GPLv3: the source has to be there).
rem This script is the only thing that regenerates it - run it after every
rem change to a .hlsl, then rebuild with be.bat.
rem
rem Every .inc gets one last line:
rem
rem     // bshader-stamp <MD5 of the .hlsl it came from>
rem
rem so that be.bat can tell, without a build and without trusting a file date,
rem whether the bytecode beside a shader is still the bytecode of that
rem shader. A date cannot be trusted for this - a fresh clone gives every
rem file the same one - so the stamp is the content.
rem
rem The content has to be the same content everywhere: git can check the
rem HLSL out with or without carriage returns depending on core.autocrlf, and
rem the stamp is taken over bytes, so a checkout with different line endings
rem computes a different hash for identical source. .gitattributes pins
rem **/hd_shader/*.hlsl and *.inc to eol=lf for exactly this line.
setlocal enabledelayedexpansion

set "SHADERDIR=%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_shader"

set "FXC="
call :find "C:\Program Files (x86)\Windows Kits\10\bin"
call :find "C:\Program Files\Windows Kits\10\bin"

if not defined FXC (
	echo bshader: no fxc.exe found in any Windows Kits 10 bin folder.
	echo          Install the Windows SDK, or point FXC at it by hand.
	exit /b 1
)

echo bshader: using "%FXC%"

"%FXC%" /nologo /T vs_3_0 /E RoomVS /O3 /Vn kHDRoomVS /Fh "%SHADERDIR%\hd_room_vs.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T ps_3_0 /E RoomPS /O3 /Vn kHDRoomPS /Fh "%SHADERDIR%\hd_room_ps.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

rem The room shader's gloss variant: the same file with HD_GLOSS defined, a
rem pixel shader of its own with its own 512 instruction slots (hd_room.hlsl,
rem "The sheen"). Stamped with the same .hlsl's MD5 as the plain one.
"%FXC%" /nologo /T ps_3_0 /E RoomPS /O3 /D HD_GLOSS=1 /Vn kHDRoomGlossPS /Fh "%SHADERDIR%\hd_room_gloss_ps.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

rem The room shader's interior variant: the same file with HD_INSIDE defined,
rem a pixel shader of its own for material 17's grilles, the cavity's page
rem read through the bars (hd_room.hlsl, "The interior"). Stamped with the
rem same .hlsl's MD5 as the plain one.
"%FXC%" /nologo /T ps_3_0 /E RoomPS /O3 /D HD_INSIDE=1 /Vn kHDRoomInsidePS /Fh "%SHADERDIR%\hd_room_inside_ps.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

rem The water sheet: the same file's WaterVS and WaterPS, a vertex and a
rem pixel shader of their own for the water surface (hd_room.hlsl, "the
rem sheet"). Stamped with the same .hlsl's MD5 as the room shaders.
"%FXC%" /nologo /T vs_3_0 /E WaterVS /O3 /Vn kHDRoomWaterVS ^
 /Fh "%SHADERDIR%\hd_room_water_vs.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T ps_3_0 /E WaterPS /O3 /Vn kHDRoomWaterPS ^
 /Fh "%SHADERDIR%\hd_room_water_ps.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

rem The sheet's pixel shader once more with HD_WATER_DEPTH defined: the
rem variant that reads the frame's depth texture where the adapter offers
rem one (hd_room.hlsl, WaterDepthBelow); the plain one is the fallback.
"%FXC%" /nologo /T ps_3_0 /E WaterPS /O3 /D HD_WATER_DEPTH=1 ^
 /Vn kHDRoomWaterDepthPS ^
 /Fh "%SHADERDIR%\hd_room_water_depth_ps.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

rem And the depth variant with HD_WATER_VIEW as well: RoomProbe's
rem -waterdepthview pictures (hd_room.hlsl, the view block); the game never
rem draws with it.
"%FXC%" /nologo /T ps_3_0 /E WaterPS /O3 /D HD_WATER_DEPTH=1 ^
 /D HD_WATER_VIEW=1 /Vn kHDRoomWaterViewPS ^
 /Fh "%SHADERDIR%\hd_room_water_view_ps.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

rem And the depth variant with HD_WATER_UNDER as well: the surface seen from
rem below while the eye is under the water - no foam, the extinction in the
rem fog's place (hd_room.hlsl, "under the water").
"%FXC%" /nologo /T ps_3_0 /E WaterPS /O3 /D HD_WATER_DEPTH=1 ^
 /D HD_WATER_UNDER=1 /Vn kHDRoomWaterUnderPS ^
 /Fh "%SHADERDIR%\hd_room_water_under_ps.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

rem The foam's own draw over a sheet that has foam: the same file's WaterPS
rem with HD_WATER_FOAM defined, plain and with the depth texture
rem (hd_room.hlsl, "the foam's draw"); the four variants above draw none.
"%FXC%" /nologo /T ps_3_0 /E WaterPS /O3 /D HD_WATER_FOAM=1 ^
 /Vn kHDRoomWaterFoamPS ^
 /Fh "%SHADERDIR%\hd_room_water_foam_ps.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T ps_3_0 /E WaterPS /O3 /D HD_WATER_DEPTH=1 ^
 /D HD_WATER_FOAM=1 /Vn kHDRoomWaterFoamDepthPS ^
 /Fh "%SHADERDIR%\hd_room_water_foam_depth_ps.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

rem The waterfalls' feet: the same file's FootPS, a second draw over a
rem sheet where a waterfall lands (hd_room.hlsl, "the waterfall's foot").
"%FXC%" /nologo /T ps_3_0 /E FootPS /O3 /Vn kHDRoomWaterFootPS ^
 /Fh "%SHADERDIR%\hd_room_water_foot_ps.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

rem The rivers' streaks and the foam where they end: the same file's RiverPS,
rem a second draw over a river's sheet (hd_room.hlsl, "the river").
"%FXC%" /nologo /T ps_3_0 /E RiverPS /O3 /Vn kHDRoomWaterRiverPS ^
 /Fh "%SHADERDIR%\hd_room_water_river_ps.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

rem The sea seen from below (HDRenderWaterLook, RoomProbe -underlook): the
rem sun's disc, the lit underside and the shafts (hd_water_look.hlsl, which
rem includes hd_room.hlsl for the waves and the hash); the game draws it in
rem a level with the deep look, the probe its study as well.
"%FXC%" /nologo /T vs_3_0 /E LookVS /O3 /Vn kHDWaterLookVS ^
 /Fh "%SHADERDIR%\hd_water_look_vs.inc" "%SHADERDIR%\hd_water_look.hlsl"
if errorlevel 1 exit /b 1
"%FXC%" /nologo /T ps_3_0 /E LookPS /O3 /Vn kHDWaterLookPS ^
 /Fh "%SHADERDIR%\hd_water_look_ps.inc" "%SHADERDIR%\hd_water_look.hlsl"
if errorlevel 1 exit /b 1

rem The water's cards - a splash's crown and drops, a tank's spray: the same
rem file's CardVS and CardPS with HD_CARD defined, which alone declares their
rem constants (hd_room.hlsl, "the cards"). Stamped with the same MD5.
"%FXC%" /nologo /T vs_3_0 /E CardVS /O3 /D HD_CARD=1 /Vn kHDRoomCardVS ^
 /Fh "%SHADERDIR%\hd_room_card_vs.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T ps_3_0 /E CardPS /O3 /D HD_CARD=1 /Vn kHDRoomCardPS ^
 /Fh "%SHADERDIR%\hd_room_card_ps.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

rem The wet floor a breaking fish tank leaves: a file of its own, drawn with
rem the cards' vertex shader (hd_wet.hlsl, hd_render.cpp "the wet floor").
"%FXC%" /nologo /T ps_3_0 /E WetPS /O3 /Vn kHDWetPS ^
 /Fh "%SHADERDIR%\hd_wet_ps.inc" "%SHADERDIR%\hd_wet.hlsl"
if errorlevel 1 exit /b 1
rem And RoomProbe's march debugger out of the same file (-tankmarch).
"%FXC%" /nologo /T ps_3_0 /E WetPS /O3 /D HD_WET_DEBUG=1 /Vn kHDWetDebugPS ^
 /Fh "%SHADERDIR%\hd_wet_debug_ps.inc" "%SHADERDIR%\hd_wet.hlsl"
if errorlevel 1 exit /b 1
rem And the marble floors' planar mirror out of the same file (MarblePS,
rem hd_render.cpp "the marble floors").
"%FXC%" /nologo /T ps_3_0 /E MarblePS /O3 /Vn kHDWetMarblePS ^
 /Fh "%SHADERDIR%\hd_wet_marble_ps.inc" "%SHADERDIR%\hd_wet.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T vs_3_0 /E ModelVS /O3 /Vn kHDModelVS /Fh "%SHADERDIR%\hd_model_vs.inc" "%SHADERDIR%\hd_model.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T ps_3_0 /E ModelPS /O3 /Vn kHDModelPS /Fh "%SHADERDIR%\hd_model_ps.inc" "%SHADERDIR%\hd_model.hlsl"
if errorlevel 1 exit /b 1

rem The glow shader includes hd_room.hlsl; compiling it here with the others
rem is what keeps its .inc in step with an edit to the room shader.
"%FXC%" /nologo /T ps_3_0 /E GlowPS /O3 /Vn kHDGlowPS /Fh "%SHADERDIR%\hd_glow_ps.inc" "%SHADERDIR%\hd_glow.hlsl"
if errorlevel 1 exit /b 1

rem The projected shadow's cast shaders include hd_model.hlsl, for the same
rem reason the glow shader includes hd_room.hlsl.
"%FXC%" /nologo /T vs_3_0 /E CastVS /O3 /Vn kHDShadowCastVS /Fh "%SHADERDIR%\hd_shadow_cast_vs.inc" "%SHADERDIR%\hd_shadow.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T ps_3_0 /E CastPS /O3 /Vn kHDShadowCastPS /Fh "%SHADERDIR%\hd_shadow_cast_ps.inc" "%SHADERDIR%\hd_shadow.hlsl"
if errorlevel 1 exit /b 1

rem The projected shadow's receive shader stands alone: it reads RoomVS's
rem outputs by semantic and includes nothing.
"%FXC%" /nologo /T ps_3_0 /E ReceivePS /O3 /Vn kHDShadowReceivePS /Fh "%SHADERDIR%\hd_shadow_receive_ps.inc" "%SHADERDIR%\hd_shadow_receive.hlsl"
if errorlevel 1 exit /b 1

rem Its two-map variant: the same file's second entry point, a pixel shader
rem of its own that reads the second map beside hers (hd_shadow_receive.hlsl,
rem Receive2PS). Stamped with the same .hlsl's MD5 as the one-map one.
"%FXC%" /nologo /T ps_3_0 /E Receive2PS /O3 /Vn kHDShadowReceive2PS /Fh "%SHADERDIR%\hd_shadow_receive2_ps.inc" "%SHADERDIR%\hd_shadow_receive.hlsl"
if errorlevel 1 exit /b 1

rem The models' variant: the same file's third entry point, the two-map body
rem on ModelVS's outputs (hd_shadow_receive.hlsl, ModelReceivePS). Stamped
rem with the same .hlsl's MD5 as the other two.
"%FXC%" /nologo /T ps_3_0 /E ModelReceivePS /O3 /Vn kHDShadowReceiveModelPS /Fh "%SHADERDIR%\hd_shadow_receive_model_ps.inc" "%SHADERDIR%\hd_shadow_receive.hlsl"
if errorlevel 1 exit /b 1

rem The mirror's variant: the same file's fourth entry point, the two-map body
rem on the copy behind a mirror's glass alone (hd_shadow_receive.hlsl,
rem MirrorReceivePS). Stamped with the same .hlsl's MD5 as the other three.
"%FXC%" /nologo /T ps_3_0 /E MirrorReceivePS /O3 /Vn kHDShadowReceiveMirrorPS /Fh "%SHADERDIR%\hd_shadow_receive_mirror_ps.inc" "%SHADERDIR%\hd_shadow_receive.hlsl"
if errorlevel 1 exit /b 1

rem The springs: a file of their own with four pixel shaders, the column,
rem the bubble, the crack's walls and its bottom, drawn with the cards'
rem vertex shader (hd_spring.hlsl, hd_spring.cpp). All stamped with the same
rem .hlsl's MD5.
"%FXC%" /nologo /T ps_3_0 /E ColumnPS /O3 /Vn kHDSpringColumnPS ^
 /Fh "%SHADERDIR%\hd_spring_column_ps.inc" "%SHADERDIR%\hd_spring.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T ps_3_0 /E BubblePS /O3 /Vn kHDSpringBubblePS ^
 /Fh "%SHADERDIR%\hd_spring_bubble_ps.inc" "%SHADERDIR%\hd_spring.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T ps_3_0 /E CrackPS /O3 /Vn kHDSpringCrackPS ^
 /Fh "%SHADERDIR%\hd_spring_crack_ps.inc" "%SHADERDIR%\hd_spring.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T ps_3_0 /E HolePS /O3 /Vn kHDSpringHolePS ^
 /Fh "%SHADERDIR%\hd_spring_hole_ps.inc" "%SHADERDIR%\hd_spring.hlsl"
if errorlevel 1 exit /b 1

rem The sky's own shaders, RoomProbe's alone: the classic flat layer
rem drawn with a page (hd_sky.hlsl, LayerVS and LayerPS). Stamped with
rem the same .hlsl's MD5.
"%FXC%" /nologo /T vs_3_0 /E LayerVS /O3 /Vn kHDSkyLayerVS ^
 /Fh "%SHADERDIR%\hd_sky_layer_vs.inc" "%SHADERDIR%\hd_sky.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T ps_3_0 /E LayerPS /O3 /Vn kHDSkyLayerPS ^
 /Fh "%SHADERDIR%\hd_sky_layer_ps.inc" "%SHADERDIR%\hd_sky.hlsl"
if errorlevel 1 exit /b 1

rem And the cloud layer, measured by RoomProbe alone (hd_sky.hlsl,
rem CloudVS and CloudPS): a hashed field marched through a thin slab.
"%FXC%" /nologo /T vs_3_0 /E CloudVS /O3 /Vn kHDSkyCloudVS ^
 /Fh "%SHADERDIR%\hd_sky_cloud_vs.inc" "%SHADERDIR%\hd_sky.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T ps_3_0 /E CloudPS /O3 /Vn kHDSkyCloudPS ^
 /Fh "%SHADERDIR%\hd_sky_cloud_ps.inc" "%SHADERDIR%\hd_sky.hlsl"
if errorlevel 1 exit /b 1

rem The gamma row's pass over the presented picture (hd_gamma.hlsl,
rem dxshell.cpp "the gamma pass"): ps_2_0, because its quad is drawn
rem pretransformed and without a vertex shader, which ps_3_0 refuses.
"%FXC%" /nologo /T ps_2_0 /E GammaPS /O3 /Vn kHDGammaPS ^
 /Fh "%SHADERDIR%\hd_gamma_ps.inc" "%SHADERDIR%\hd_gamma.hlsl"
if errorlevel 1 exit /b 1

rem The fog bulbs' body (hd_fog.hlsl, FogVS and FogPS): a quad per bulb
rem over the frame, the bulb's density integrated to the frame's depth.
"%FXC%" /nologo /T vs_3_0 /E FogVS /O3 /Vn kHDFogVS ^
 /Fh "%SHADERDIR%\hd_fog_vs.inc" "%SHADERDIR%\hd_fog.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T ps_3_0 /E FogPS /O3 /Vn kHDFogPS ^
 /Fh "%SHADERDIR%\hd_fog_ps.inc" "%SHADERDIR%\hd_fog.hlsl"
if errorlevel 1 exit /b 1

call :stamp hd_room hd_room_vs.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_gloss_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_inside_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_water_vs.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_water_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_water_depth_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_water_view_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_water_under_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_water_foot_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_water_river_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_water_foam_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_water_foam_depth_ps.inc
if errorlevel 1 exit /b 1
rem The water look includes hd_room.hlsl, the shadow caster hd_model.hlsl:
rem their .inc carry that file's MD5 too, as the glow shader's does below.
call :incstamp hd_room hd_water_look_vs.inc
if errorlevel 1 exit /b 1
call :stamp hd_water_look hd_water_look_vs.inc
if errorlevel 1 exit /b 1
call :incstamp hd_room hd_water_look_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_water_look hd_water_look_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_card_vs.inc
if errorlevel 1 exit /b 1
call :stamp hd_room hd_room_card_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_wet hd_wet_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_wet hd_wet_debug_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_wet hd_wet_marble_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_model hd_model_vs.inc
if errorlevel 1 exit /b 1
call :stamp hd_model hd_model_ps.inc
if errorlevel 1 exit /b 1
rem The glow shader reads hd_room.hlsl's CausticOf as well: its .inc carries
rem that file's MD5 on a line of its own, before its own stamp (be.bat).
call :incstamp hd_room hd_glow_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_glow hd_glow_ps.inc
if errorlevel 1 exit /b 1
call :incstamp hd_model hd_shadow_cast_vs.inc
if errorlevel 1 exit /b 1
call :stamp hd_shadow hd_shadow_cast_vs.inc
if errorlevel 1 exit /b 1
call :incstamp hd_model hd_shadow_cast_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_shadow hd_shadow_cast_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_shadow_receive hd_shadow_receive_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_shadow_receive hd_shadow_receive2_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_shadow_receive hd_shadow_receive_model_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_shadow_receive hd_shadow_receive_mirror_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_spring hd_spring_column_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_spring hd_spring_bubble_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_spring hd_spring_crack_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_spring hd_spring_hole_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_sky hd_sky_layer_vs.inc
if errorlevel 1 exit /b 1
call :stamp hd_sky hd_sky_layer_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_sky hd_sky_cloud_vs.inc
if errorlevel 1 exit /b 1
call :stamp hd_sky hd_sky_cloud_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_gamma hd_gamma_ps.inc
if errorlevel 1 exit /b 1
call :stamp hd_fog hd_fog_vs.inc
if errorlevel 1 exit /b 1
call :stamp hd_fog hd_fog_ps.inc
if errorlevel 1 exit /b 1

rem fxc and echo end their lines with CRLF, while .gitattributes keeps the
rem .inc LF: without this pass git shows every .inc modified after every
rem run, in their line endings alone. Rewritten with line feeds, a
rem regenerated .inc is byte for byte the tracked one.
set "PS=$d = $env:SHADERDIR; foreach ($f in Get-ChildItem -LiteralPath $d"
set "PS=!PS! -Filter *.inc) { $t = [IO.File]::ReadAllText($f.FullName);"
set "PS=!PS! $crlf = [string][char]13 + [char]10;"
set "PS=!PS! [IO.File]::WriteAllText($f.FullName,"
set "PS=!PS! $t.Replace($crlf, [string][char]10)) }"
powershell -NoProfile -NonInteractive -Command "!PS!"
if errorlevel 1 (
	echo bshader: the .inc could not be rewritten with line feeds.
	exit /b 1
)

for %%F in ("%SHADERDIR%\hd_room_vs.inc" "%SHADERDIR%\hd_room_ps.inc" "%SHADERDIR%\hd_room_gloss_ps.inc" "%SHADERDIR%\hd_model_vs.inc" "%SHADERDIR%\hd_model_ps.inc" "%SHADERDIR%\hd_glow_ps.inc" "%SHADERDIR%\hd_shadow_cast_vs.inc" "%SHADERDIR%\hd_shadow_cast_ps.inc" "%SHADERDIR%\hd_shadow_receive_ps.inc" "%SHADERDIR%\hd_shadow_receive2_ps.inc" "%SHADERDIR%\hd_shadow_receive_model_ps.inc" "%SHADERDIR%\hd_shadow_receive_mirror_ps.inc") do echo bshader: %%~nxF %%~zF bytes of C source
call :size "%SHADERDIR%\hd_room_water_vs.inc"
call :size "%SHADERDIR%\hd_room_water_ps.inc"
call :size "%SHADERDIR%\hd_room_water_depth_ps.inc"
call :size "%SHADERDIR%\hd_room_water_view_ps.inc"
call :size "%SHADERDIR%\hd_room_card_vs.inc"
call :size "%SHADERDIR%\hd_room_card_ps.inc"
call :size "%SHADERDIR%\hd_wet_ps.inc"
call :size "%SHADERDIR%\hd_wet_debug_ps.inc"
call :size "%SHADERDIR%\hd_wet_marble_ps.inc"
call :size "%SHADERDIR%\hd_spring_column_ps.inc"
call :size "%SHADERDIR%\hd_spring_bubble_ps.inc"
call :size "%SHADERDIR%\hd_spring_crack_ps.inc"
call :size "%SHADERDIR%\hd_spring_hole_ps.inc"
call :size "%SHADERDIR%\hd_sky_layer_vs.inc"
call :size "%SHADERDIR%\hd_sky_layer_ps.inc"
call :size "%SHADERDIR%\hd_sky_cloud_vs.inc"
call :size "%SHADERDIR%\hd_sky_cloud_ps.inc"
call :size "%SHADERDIR%\hd_gamma_ps.inc"
call :size "%SHADERDIR%\hd_fog_vs.inc"
call :size "%SHADERDIR%\hd_fog_ps.inc"
exit /b 0

:stamp
rem %1 the .hlsl stem, %2 the .inc file it was compiled into.
set "HASH="
for /f "skip=1 delims=" %%H in ('certutil -hashfile "%SHADERDIR%\%~1.hlsl" MD5') do if not defined HASH set "HASH=%%H"
if not defined HASH (
	echo bshader: certutil could not hash %~1.hlsl - no stamp written.
	exit /b 1
)
set "HASH=!HASH: =!"
>>"%SHADERDIR%\%~2" echo // bshader-stamp !HASH!
exit /b 0

:incstamp
rem %1 the stem of an .hlsl the shader includes, %2 the .inc: the line
rem "// bshader-include <stem> <MD5>", which be.bat's :incstamp compares.
set "HASH="
set "G=%SHADERDIR%\%~1.hlsl"
for /f "skip=1 delims=" %%H in ('certutil -hashfile "!G!" MD5') do (
	if not defined HASH set "HASH=%%H"
)
if not defined HASH (
	echo bshader: certutil could not hash %~1.hlsl - no stamp written.
	exit /b 1
)
set "HASH=!HASH: =!"
>>"%SHADERDIR%\%~2" echo // bshader-include %~1 !HASH!
exit /b 0

:size
rem %1 an .inc file: its name and size, as the line above prints them.
echo bshader: %~nx1 %~z1 bytes of C source
exit /b 0

:find
rem Newest SDK first - /o-n sorts the version folders descending by name.
if not exist "%~1" exit /b 0
for /f "delims=" %%D in ('dir /b /ad /o-n "%~1" 2^>nul') do (
	if not defined FXC if exist "%~1\%%D\x86\fxc.exe" set "FXC=%~1\%%D\x86\fxc.exe"
)
exit /b 0
