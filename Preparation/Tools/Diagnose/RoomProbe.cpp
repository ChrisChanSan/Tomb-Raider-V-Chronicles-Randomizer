/*
 * RoomProbe - draw one HD room with the engine's own renderer and write a PNG.
 *
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 *
 * specific\hd\hd_render.cpp is a second draw path beside the engine's
 * fixed-function one. A renderer nobody has seen a picture out of is not a
 * renderer, it is a guess - so this tool gives it a hidden window, a real
 * Direct3D 9 device, a render target and a camera, and puts what comes back
 * into a .PNG. Same source files as the engine links, no copy: what the
 * picture shows is what the game will draw.
 *
 * It also prints three measurements:
 *
 *   - the depth proof. The classic path writes its own depth as
 *     sz = f_a - f_boo * rhw; the HD path lets the device compute it from a
 *     projection matrix. If the two disagree the shared depth buffer is a
 *     fiction. HDRenderSelfTest compares them over 50 view distances, and
 *     HDRenderDepthTruth reads back what the device actually compares with -
 *     ZENABLE, ZFUNC, ZWRITEENABLE and the viewport's MinZ / MaxZ - because
 *     a proof against an assumed state proves nothing.
 *   - the world-matrix proof (-matrixcheck). InsertRoom hands the HD path the
 *     matrix that is on the engine's stack for a room, and the HD path
 *     multiplies it by a projection matrix and lets the device divide by w.
 *     The classic path multiplies by the same matrix and divides by z itself.
 *     If those two do not put the same room-local point in the same pixel,
 *     every HD wall stands beside its own floor - so both are computed here,
 *     over up to 128 of the room's own vertices, and the worst difference is
 *     printed in pixels.
 *   - the page cache in both pools. D3DPOOL_DEFAULT pushes every page through
 *     one reused system-memory staging texture; D3DPOOL_MANAGED lets Direct3D
 *     keep a system copy of all of them. The engine is a 32 bit process, so
 *     the number that decides between them is the peak commit of the process,
 *     and that is what this tool reports (which is also why it is built x86
 *     and not x64 like its neighbours - on x64 the question has no meaning).
 *
 * USAGE
 *
 *   RoomProbe <5-folder> <LEVEL> <room> [x y z yaw pitch] [options]
 *
 *     <5-folder>  the remaster's game 5 folder - the one with DATA and TEX
 *     <LEVEL>     level stem without extension, e.g. ANDREA1
 *     <room>      .TRG group number = room number
 *     x y z       camera, room-local classic units (y counts DOWNWARDS)
 *     yaw pitch   degrees; yaw 0 looks along +Z, 90 along +X,
 *                 pitch > 0 looks down
 *
 *   Without x..pitch the camera is placed in the middle of the room looking
 *   at its longest wall, which is what a first look wants.
 *
 *   Options
 *     -size <w> <h>   render target, default 1280 720
 *     -fov <deg>      default 80, the engine's own
 *     -all            three more PNGs, one per pass (opaque, alpha, additive)
 *     -managed        pages in D3DPOOL_MANAGED instead of D3DPOOL_DEFAULT
 *     -threads <n>    the level load's threads (HDPoolRun): the page
 *                     decode, the vertex buffer, the index fill per
 *                     group, the mirror search; 0 = the machine's own
 *                     count, at most 16; default 8. The build's hash
 *                     is the same for every n
 *     -rooms <n>      the classic room count, so groups beyond it count as sky
 *     -out <prefix>   output path prefix, default .\roomprobe
 *     -groups         list every group's triangles and ranges, then stop
 *     -nodraw         load and measure only - no window, no picture
 *     -matrixcheck    the world-matrix proof as a table, then stop
 *     -sky            draw the .TRG's sky group(s) behind the room, centred
 *                     on the camera, in their own slice of the depth buffer -
 *                     what the engine does in place of the classic horizon
 *                     mesh
 *     -skyonly        the same, with the room left out: the dome alone
 *     -skycensus      what the level's sky is made of, printed and then
 *                     stop, no device: per sky part and pass the material,
 *                     the pages, the height, radius, elevation, UV bytes and
 *                     light (`sky part`), the band's rings (`sky ring`), the
 *                     sky pages of the level's page table (`sky pages`) and
 *                     the classic HORIZON mesh's rings (`sky horizon`, with
 *                     -roombox). -rooms or -roombox gives the room count
 *     -skydump <path> with -skycensus: every sky triangle as one line,
 *                     group part pass page and x y z u v of its corners
 *     -skylayer <1|2> <r> <g> <b> <vel>
 *                     with -sky: one of the script's classic flat layers
 *                     over the HD sky as SkyDrawPhase draws it, moved as
 *                     UpdateSky moves it over -time seconds; repeatable
 *     -skylayerpage sd|hd|psx  the page the layers carry: the level's
 *                     classic sky page out of the .TRC under -roombox (sd),
 *                     the HD sky's nearest cloud sheet page (hd), or the PSX
 *                     sky's tiles on the SKY_GRAPHICS sprites' page (psx)
 *     -skycloud <floor> <thickness> <cell> <coverage> <density> <vel>
 *                     with -sky: a volumetric cloud layer over the HD sky
 *                     and the layers, a measurement only - a hashed
 *                     field marched through a slab over the camera, moved
 *                     as UpdateSky moves a layer of speed `vel`
 *     -skygpu <n>     after the picture, the HD sky, the layers and the
 *                     cloud layer each timed n times on the GPU (`sky gpu`
 *                     lines: median, min, max); the picture of such a run
 *                     holds the extra draws
 *     -with <group> <dx dy dz>
 *                     draw that group too, at that world offset from the
 *                     room - the difference of the two classic room origins,
 *                     which RoomCheck prints per room. Up to 256, as many as
 *                     draw_rooms holds - repeat the switch; a 257th is
 *                     refused with a message and nothing is drawn.
 *     -oldorder       draw the scene room-major, all three passes of one room
 *                     before the next, instead of the way HDFrameDraw draws
 *                     it: every room's solid passes, then every room's
 *                     additive one.
 *     -black          clear the picture to black instead of magenta. The
 *                     additive pass alone can only be measured against black.
 *     -noshore        the swell reaches the water's own edge at full height,
 *                     instead of tapering off at the shore - the before
 *                     picture out of the same build.
 *     -noupright      the wall vertices at the waterline keep the surface's
 *                     UV wobble and its caustic, instead of holding it back -
 *                     the before picture out of the same build.
 *     -noblend        material 14 stays in the alpha TEST instead of a real
 *                     blend - the glass drawn as a hard mask of itself, the
 *                     before picture out of the same build.
 *     -noclear        the alpha-pass triangles on a page that is part-way
 *                     clear in every texel stay in the alpha TEST instead of
 *                     being blended as a clear floor - RICH2's marble drawn
 *                     solid over the copy of the room beneath it, the before
 *                     picture out of the same build.
 *     -glow           two more PNGs after the full frame, both of them the
 *                     additive pass of every drawn group ALONE over black:
 *                     <prefix>_<LEVEL>_r<room>_glow.png keeps the full
 *                     frame's depth buffer, so a wall or a pillar in front
 *                     of a light shaft cuts it exactly as it does in the
 *                     game, and ..._glow_nodepth.png clears it first - the
 *                     picture -all's additive shot gives for one room. The
 *                     glow's own brightness can only be read against black,
 *                     and what the player sees of it only through the
 *                     scene's depth; this is both at once.
 *     -stackedseas    draw every copy of a sea the .TRG stacks three times
 *                     or more (JOBY4 room 19, JOBY5 rooms 65, 82 and 117)
 *                     instead of the one copy the renderer keeps - the
 *                     before picture out of the same build.
 *     -fastswing      let a material 2 part swing whatever its period,
 *                     instead of drawing the ones under half a second still
 *                     - the before picture out of the same build.
 *     -addmat <n>     the additive pass draws only the ranges of .TRG
 *                     material n (2 the glows, 16 the seas): one material's
 *                     glow alone, for telling which of them moved a pixel.
 *     -bookpage <n>   every .TRG material 15 book shows page n of its own
 *                     run instead of the page the clock names - one book
 *                     drawn page by page, to find its brightest page.
 *                     Without it -time turns the books like everything else
 *                     that moves (the page is whole steps of -time plus the
 *                     part's phase), and -nomotion leaves them on page 0.
 *                     One `book` line per material 15 part and pass says
 *                     what the build made of it.
 *     -glowblend add|screen
 *                     how material 2 composes in the additive pass: screen
 *                     is the renderer's own, add the ONE / ONE sum every
 *                     other additive material keeps - the before picture out
 *                     of the same build.
 *     -aniso 0|1      material 2 sampled with the anisotropic filter (1, the
 *                     renderer's own) or with the trilinear one of every
 *                     other draw call (0).
 *     -glowps 0|1     material 2 drawn with the glow pixel shader and its
 *                     fades (1, the renderer's own) or with the room pixel
 *                     shader (0) - the before picture out of the same build.
 *                     The additive block lists what the level build decided
 *                     for every glow part.
 *     -cull none|opaque
 *                     the opaque room ranges - pass 0 with no alpha test, no
 *                     water and no sky - drawn with their back faces culled
 *                     (opaque, the renderer's own) or two-sided (none), the
 *                     before picture out of the same build. Every other
 *                     range is two-sided either way.
 *     -roomwin <g>:<l>,<t>,<r>,<b>[;<g>:...]
 *                     the window the walk gave each named group, as the
 *                     walk prints it (-walkwant): that group is drawn the
 *                     way hd_level.cpp's DrawRoomWindowed draws it - the
 *                     open half and the pieces of intruding art answered
 *                     whole over the whole screen, the rest inside the
 *                     widened window. Needs -roombox.
 *     -decide whole|scissor|frame|face
 *                     the answer for those pieces: every one whole, every
 *                     one inside the window, the engine's own per-frame
 *                     answer (frame, the default: a piece whose holder is
 *                     not drawn is whole; otherwise a claimed piece is whole
 *                     when any claimed classic face of its pair survives the
 *                     corner rule against the group's window, and the
 *                     triangles no face claims stay in the window; an empty
 *                     window asks no face and draws the pieces outside the
 *                     box one sector smaller whole and the others not at
 *                     all), or the
 *                     same with each piece held to its own face alone
 *                     (face). One `decide:` line per piece says which, a
 *                     `decide: reachart` line per holder of an empty-window
 *                     group gives the engine's reachart fields but reach=,
 *                     and a `decide: clip` line after the picture the
 *                     counters of the engine's HDLOG clip line over the
 *                     named groups that have art. -roomwin takes up to 256
 *                     groups, like -with. A holder counts as drawn when it
 *                     is the seed or any -with group; the engine lists only
 *                     the groups it draws in HD, so the groups the log's
 *                     HDLOG room= lines give hd=0 stay out of -with.
 *     -unlinked 0|1|2 1, the default and the engine's answer, draws a piece
 *                     whose art stands in the air of a room its own room
 *                     has no link with (HDRoomAir), and that an intrusion
 *                     box holds, only inside the group's window, under
 *                     frame and face, and not at all when the window is
 *                     empty - but for a piece of a pair with a claimed
 *                     face that survives the window, which keeps the
 *                     answer above; 0 leaves those pieces the answer
 *                     above, out of the same build; 2 is 1 but for a
 *                     group whose window is empty, whose pieces keep the
 *                     smaller box's answer - the other answer for such a
 *                     room, out of the same build. The `decide: clip`
 *                     line ends with the engine's unlinked_ counters.
 *     -air 0|1|2      the engine's tomb5.hd_air: 1, the default, hands the
 *                     rooms' air to the build; 0 hands none, and the build
 *                     and its pictures are the box's alone, as the engine's
 *                     with hd_air 0; 2 holds the hulls' art in air no
 *                     box holds to their own windows too (MarkAirHulls).
 *                     The intrusion line ends with air=.
 *
 *   The portal walk (game\draw.cpp) offline, with the engine's own camera:
 *   the integer angles phd_LookAt makes of an eye and a target, the float
 *   sine table the rotation is built from and AlterFOV's integer
 *   perspective, so that a list and its windows are the engine's to the
 *   pixel. With -reach the list is trimmed as ReachTrimRooms trims it: a
 *   room the reach alone brings in with no .TRG art goes, and the list is
 *   held to the budget of 128.
 *
 *     -walk <seed room> <x y z>
 *                     the walk from that eye (world units); -walkyaw,
 *                     -walkpitch and -walkpitchfan name the directions, in
 *                     degrees rounded once to the engine's angle units
 *     -walktarget <x y z>
 *                     one direction instead, built from the eye and this
 *                     world point as phd_LookAt builds it - the eye and
 *                     target= of a ROOMLIST whole-list dump. Without -walk
 *                     it sets the picture's camera: the room-local x y z of
 *                     the command line and this target give the yaw, the
 *                     pitch and the perspective the engine would use, and
 *                     -decide frame holds the classic faces against them.
 *                     Needs -roombox for the room's origin.
 *     -walkwant <room>
 *                     why that room is or is not in the list and, with
 *                     -walkeach, its final window and reach after the walk:
 *                     `final=l,t..r,b reach=l,t..r,b classic|REACH`
 *     -walkfinal      the same final line for every listed room (-walkeach)
 *     -walkdoors      one line per closed door: its item, slot, room and
 *                     place, the room behind it and the world corners of
 *                     each portal it shuts
 *     -walkshut <item,item,...|all>
 *                     walk with those closed doors shut (CLOSED_DOOR1 ..
 *                     LIFT_DOORS2 items, whose shutting zeroes the normals
 *                     of the portal they stand in); every other door open
 *     -flip <room,room,...>
 *                     walk with those rooms' flipmaps swapped, as FlipMap
 *                     swaps them (game\control.cpp): each named room - the
 *                     one whose flipped_room names its twin, as the log's
 *                     HDLOG flip lines name it first - takes its twin's
 *                     record and is drawn by the twin's group (HDFlipRoom).
 *                     One `-flip:` line per pair names the state; the
 *                     walk's lists stay room numbers, each drawn by the
 *                     group the line names. With -hdwater 1 the water
 *                     follows the swap too (HDRenderWaterFlipRoom): the
 *                     bodies only an alternate floods draw. With -smash
 *                     the shots name the rooms after the swaps.
 *     -walkdump <log> [frame]
 *                     every ROOMLIST whole-list dump of a -log run, or the
 *                     one of that frame, walked again - see RunWalkDump.
 *                     -roombox must hold the .TRC files that were played.
 *
 *   The parity block. Every one of these makes up a piece of engine state
 *   the tool has no engine for, and hands it to the very structures
 *   hd_level.cpp fills in the game.
 *
 *     -light <x y z r g b falloff>
 *                     one dynamic light, room-local position (the printed
 *                     room box says the range), colour 0..1, falloff in
 *                     classic units - a torch. Up to 8, repeat the switch.
 *     -bulb <x y z radius density [r g b]>
 *                     one fog bulb, room-local; density is the engine's own
 *                     bulb->d (1 / den, so 0.02 is a thick one). Colour
 *                     defaults to white. Up to HD_MAX_BULBS (8), repeat
 *                     the switch.
 *     -fog <start end r g b>
 *                     distance fog. Only `start` reaches the picture, in
 *                     WORLD units - TR5's fog has no end and no colour: the
 *                     classic path subtracts a linear ramp of 255 over the
 *                     same distance again from the vertex colour and never
 *                     blends towards anything (drawroom.cpp). The other
 *                     four are read and reported so the switch means what
 *                     it says rather than silently something else.
 *     -underwater     the room carries ROOM_UNDERWATER and the camera is in
 *                     it: the WaterTable darkening on the colour and the
 *                     screen wobble on the position.
 *     -underlook      with -sky, -underwater and -hdwater 1 in a level
 *                     with the deep look: the sea seen from below as the
 *                     game draws it (HDRenderWaterLook) - the sun's disc
 *                     through the surface, its lit underside and the
 *                     shafts - right after the sky and before the rooms,
 *                     at HDWaterLookKnobs' numbers, its GPU time printed
 *     -underlookknobs <disc shafts underside reach power cell floor deep len
 *                     waves rim>
 *                     the same with these eleven numbers in place of the
 *                     engine's (strengths, the surface light's reach, the
 *                     disc's power, the shafts' cell size, the noise's
 *                     floor, their reach under the surface, the march's
 *                     length, the way the waves' slope is kept whole over,
 *                     the window rim's width in cosine)
 *     -underlookset <disc shafts underside reach power cell floor deep len>
 *                     the study: the same shader added over the whole
 *                     frame (HDRenderWaterProbeLook), its surface the body's
 *                     the extinction measures against; 3 3 0.25 40000 600
 *                     700 0.55 6000 8000 were its first numbers
 *     -undertick <file> <room>
 *                     with -roombox, -out and the picture's switches (-vis
 *                     1 among them; with the look's switches -sky and
 *                     -rooms <n> too): the sea's surface counted along a
 *                     walk of eyes, each a CAMLOG tick's eye and target -
 *                     the walk's rooms, the picture with them and without
 *                     <room> and the look; see UnderTicks. With
 *                     -surfacealways <room> joins the first picture where
 *                     the walk leaves it out, as the game draws the sea's
 *                     surface at every frame (hd_level.cpp, HDFrameDraw)
 *     -surfaceroom    with -hdwater: the room whose art is the sea's
 *                     surface (hd_water_under.h, HDWaterSurfaceRoom) out
 *                     of the classic level's squares and the groups' art,
 *                     as the game finds it, printed as one line
 *     -surfacealways  with -hdwater: that room drawn as the game draws it
 *                     at every frame of a level that takes the deep look,
 *                     added to the -with list where the picture leaves it
 *                     out (hd_level.cpp, HDFrameDraw)
 *     -roomplane <y x0 z0 x1 z1>
 *                     the same, on the OTHER shimmer bit - the answer a
 *                     ROOM_REFLECT + ROOM_REFLECT_CEILING room gives, which
 *                     is what the Rome levels' water carries. Both switches
 *                     now also set the room's surface height and its
 *                     selector, as the engine does.
 *     -plane <y x0 z0 x1 z1>
 *                     four SURFACE water marks at the corners of that box,
 *                     room local, as the classic path leaves them on the
 *                     corners of one water quad. The renderer floods the
 *                     plane between them, and reports how many HD vertices
 *                     the four corners were missing.
 *     -watermarks     the classic water marks the engine hands in at level
 *                     load, out of -roombox's .TRC by a twin of
 *                     hd_level.cpp's BuildWaterMarks: the art the marks name
 *                     (the art over a sheet, the flood) as in the game, the
 *                     count printed as the engine's log has it, then the
 *                     portals the closed doors shut first, as the level
 *                     load does (doors_shut=). With -plane
 *                     its four marks are added. The shimmer selector stays
 *                     off - the engine sets it per room from the room's
 *                     flags, the probe has one set for the whole picture.
 *     -hdm            apply the .hdm sidecar beside the classic .TRC the way
 *                     hd_level.cpp does, with the engine's own reader:
 *                     every opening the randomizer cut is taken out of
 *                     every group's art at build time, and what each box
 *                     caught is printed per room; a room the sidecar names
 *                     WHOLE is not drawn at all, which is the hole the
 *                     engine leaves for the classic path to fill. Needs
 *                     -roombox, because that is the folder the sidecar
 *                     lives in. Without the switch the very same files draw
 *                     with every room whole and the doorway still shut,
 *                     the before picture out of the same build.
 *     -classicroom <room>  -classicmark <room>
 *                     that room's classic faces out of the .TRC, flat grey
 *                     (red with -classicmark, which says whose they are),
 *                     in the same depth buffer as the art; repeat for more
 *                     rooms. Needs -roombox. -classiconly draws them
 *                     alone; -coincide <units> counts the pixels at which
 *                     a classic face lands within <units> of the art and,
 *                     of those, the ones the art wins the depth test at;
 *                     -coincidemark paints the first yellow and the second
 *                     red.
 *     -classicplane <y>  of the -classicroom faces only those with every
 *                     corner at height y; -classicwet then leaves out
 *                     the faces hd_level.cpp's BuildWaterSurfaces
 *                     leaves out beside a sheet - over a square with no
 *                     water room below, where the art draws flat in
 *                     that plane. The two draw what the classic path
 *                     would restore in a room's water plane.
 *     -stand          the frame's rule for a room drawn classic that another
 *                     group's art stands on (specific\hd\hd_cover.h): the
 *                     -classicroom rooms count as listed, the seed and the
 *                     -with groups as the listed art. A room the art covers
 *                     loses its classic faces, a room its carrier covers in
 *                     part the covered ones; the art lying on the faces it
 *                     keeps is left out, the art reaching onto them is drawn
 *                     pushed back; the carrier's art of a room drawn as art
 *                     whose group is not listed is drawn with that group's
 *                     offset over the whole screen, pushed back where it
 *                     also stands on a kept face (hd_cover.h,
 *                     HDStandDrawFaces). Prints `stand:` lines per holder
 *                     and range. Needs -roombox, which hands the renderer
 *                     the faces as hd_level.cpp's BuildStandFaces does.
 *     -standreceive   -stand, and after the clear phase the projected
 *                     shadow's receive of each range drawn in a holder's
 *                     window (HDRenderRoomShadowStand), counted with an
 *                     occlusion query at the depth the range was drawn at
 *                     and at the other depth push: `standreceive:` lines.
 *                     The receive tests depth EQUAL, so the first count is
 *                     the range's visible pixels and the second 0.
 *     -skipgroup <g>  group g is left out of the picture and counts as not
 *                     listed - the seed or a -with group; repeat for more.
 *     -statictable    every static mesh of the level answered as
 *                     hd_level.cpp's BuildStaticTables answers it (the same
 *                     HDRenderStaticArt), beside the answer of the rule
 *                     before it; the shares binned; every shatterable static
 *                     with the pieces it pairs with. Prints and stops. Needs
 *                     -roombox.
 *     -statics new|old  the classic statics a frame of this picture draws -
 *                     of the seed, the -with groups and the -classicroom
 *                     rooms - flat, beside the art; new is this engine's
 *                     rule, old the one before it. With new, a static held by
 *                     groups none of which the picture lists is drawn as its
 *                     piece (hd_cover.h, HDStaticDrawSource), in the frame
 *                     with the groups, over the whole screen like them. With
 *                     -coincide a `coincide statics:` line of their own.
 *     -piece <room>:<mesh>  with -statics new, only the statics named, each
 *                     as its piece or as the classic static, as the rule
 *                     answers. Repeatable.
 *     -listroom <room>  that room counts as listed for the statics and
 *                     nothing of it is drawn - a static's piece alone in a
 *                     picture. Repeatable.
 *     -static <room>:<mesh>  that static whatever a rule says; repeatable.
 *     -staticsonly    the statics alone over a cleared picture.
 *     -watersurvey <folder holding the classic .TRC>
 *                     the offline twin of hd_level.cpp's BuildWaterSurfaces:
 *                     per reflecting room of the classic level, the faces in
 *                     its minfloor plane and the answer HDRenderGroupFlatArt
 *                     gives for each of them, then the majority the engine
 *                     takes. Prints and stops.
 *     -flatart <y x0 z0 x1 z1>
 *                     does the drawn group draw anything in that horizontal
 *                     plane? The question hd_level.cpp asks per classic water
 *                     face of an HD room: a NO is a plane the remaster left
 *                     empty, and there the level's own water surface is drawn
 *                     on top of the HD room. Room local. Repeat the switch,
 *                     up to eight.
 *     -noreflect      the gloss off - the picture before it was added, out of
 *                     the same build.
 *     -glossmax       every draw call that can take the sheen takes it at
 *                     strength 1 and F0 1 - the sheen's upper bound. The
 *                     seed and the light field need -roombox.
 *     -glossfield [colour|direction]
 *                     every draw call that can take the sheen draws the
 *                     level's light field in place of its surface: its
 *                     colour times its one-sidedness (the default), or the
 *                     direction towards the light biased into a colour.
 *                     Implies -glossmax. Needs -roombox.
 *     -fieldsight     build the light field with the sight test by the rooms:
 *                     a lamp lights only the rooms within two portals of its
 *                     own - the comparison with the engine's field, which
 *                     has no sight test. Needs -roombox.
 *     -glossfile <path>
 *                     build with that hd_gloss.txt (the game reads the one
 *                     beside its executable); none without it.
 *     -hdwater 0|1    the water sheets, built as hd_level.cpp's
 *                     BuildWaterSheets builds them - the same
 *                     HDRenderSetWaterSheets over the classic level's floor
 *                     squares: 1 with the sheets (tomb5.hd_water 1), 0 with
 *                     the bodies, their wave on the remaster's water art
 *                     and no sheet and no caustic under them. A room
 *                     counts as drawn HD by the engine's own test (a
 *                     group with triangles, not hollow, with -hdm not
 *                     marked). Prints the WATERLOG lines the engine
 *                     writes and, after the picture, the GPU time of
 *                     the sheet draws. With
 *                     -nodraw it is the sweep of one level. Needs -roombox.
 *     -waterfile <path>
 *                     build with that hd_water.txt; none without it.
 *     -nocopy         the sheets without the frame's copy: the floor is
 *                     dimmed by one scalar and not bent (with -nodepth the
 *                     look the sheets had before they read the frame).
 *     -nodepth        the picture's depth buffer is a D24S8 surface and the
 *                     sheets read the vertex depth - the fallback of an
 *                     adapter without INTZ. Without it the depth buffer of
 *                     every picture is an INTZ texture where the adapter
 *                     offers one (HDRenderWaterDepthBind), with or without
 *                     -hdwater, so that the classic and the HD path are
 *                     proved to draw into it unchanged.
 *     -waterdepthview 1|2|3|4|5
 *                     the sheets draw the depth texture as grey: 1 the
 *                     path through the water over 16384 units, 2 the water
 *                     depth out of it over the vertex depth, times 0.5 (mid
 *                     grey where the two agree); 3 the sheet with the pixels
 *                     whose bent place the guard refused in red; 4 the
 *                     depth under the surface at rest over 64 units, with
 *                     nothing clipped - what the clip measures; 5 the sheet
 *                     with the bank foam's patches in green, as strong as
 *                     they are, nothing clipped.
 *     -waterbend <s>  the refraction times s, for a picture that shows what
 *                     the depth texture's mask refuses; 1 is the look built.
 *     -ripple x,z,age[,size]
 *                     a ring on the sheets at world x, z (units, as the
 *                     game's own ripples), age seconds after it began, of
 *                     a ripple that began at size (120, a wading Lara's,
 *                     without it); repeatable. Needs -hdwater 1.
 *     -splash x y z t  a splash set up at world x, y (the water's surface),
 *                     z and drawn t seconds after it began, through the
 *                     HD form (hd_render.cpp, "the cards"): its rings on
 *                     the sheet and its crown and drops. -splashkind 0 is
 *                     Lara's dive (Splash, the default), 1 her wading
 *                     splash (WadeSplash), 2 an explosion under the water
 *                     (TriggerUnderwaterExplosion); -splashfall n her fall
 *                     speed, or the explosion's depth (128). Needs
 *                     -hdwater 1; off without it.
 *     -tank item t    the fish tank `item` (FISHTANK) of the level t
 *                     seconds after it broke: its spray as cards out of
 *                     the points ControlFishtank sprays from, the film on
 *                     its room's floor with its strength of that moment and,
 *                     while the tank runs, its water ("the tank's water":
 *                     its FISHTANK mesh drawn as the water, the burst out
 *                     of the glass) (t under 0 the tank before it breaks).
 *                     Needs -hdwater 1; off without it.
 *     -tankleave spray|water  -tank without its spray, or without its
 *                     water - a part's own pixels, against the picture with
 *                     it; repeatable.
 *     -wetstand x,z[,h[,a]]  a stand-in for Lara on a wet floor at world
 *                     x, z: a dark box 160 across and h high (768, her
 *                     standing height, without it), drawn among the frame's
 *                     objects and through the wet floor's mirror of the
 *                     models; repeatable. Needs -tank and a wet square
 *                     there. a: the alpha its pixels write (255; the last
 *                     given counts for all boxes), as a model's shader
 *                     writes its page's alpha, which on her body page lies
 *                     under 64 on a tenth of the texels.
 *     -wetnomodels    -wetstand's boxes without the models' mirror: the
 *                     picture the march alone makes of them.
 *     -wetlara x,y,z[,yaw[,pose]]
 *                     Lara herself on a wet floor at world x, z, her
 *                     floor at y: the level's outfit built as the engine
 *                     builds it, her parts drawn through the model path
 *                     on the classic skeleton, standing (pose 0) or on all
 *                     fours (1), turned by yaw degrees; drawn among the
 *                     frame's objects and through the wet floor's mirror
 *                     of the models. Needs -tank; -wetnomodels leaves her
 *                     mirror out.
 *     -wetagain       the wet floor's two draws once more after the
 *                     frame's, each timed (GPU and the processor's time to
 *                     the GPU's end, the "cpu" of the -hdwater lines): the
 *                     first wet frame's cost against a second's.
 *     -wetblur k[,near]  the soft mirror at strength k: the blur's radius
 *                     per pixel the march's last step spans (hd_render.cpp,
 *                     kWetBlur), whole from near units from the eye on
 *                     (kWetBlurNear); 0 draws the mirror sharp, as before
 *                     the soft mirror. With -wetlara or -wetstand the room
 *                     behind the models' copy prints its GPU time.
 *     -wetnomirror    the wet floor's darkening without its mirror: the
 *                     picture less this one is the mirror alone, the soft
 *                     mirror's ripple metric's layer.
 *     -wetstep x0,y0,x1,y1  the picture's luma (0.299 r + 0.587 g + 0.114 b)
 *                     along the segment from pixel x0, y0 to x1, y1, one
 *                     sample a pixel of its longer side, and the largest
 *                     step between two neighbouring samples: a wetstep line
 *                     after the picture's own. Across the wet floor's
 *                     outline it measures how hard the outline is (the
 *                     water's mirror against the marble floors'); any
 *                     picture, with or without -tank.
 *     -wetcut r,b,g,row  the pixels of one lit colour counted above and
 *                     below a row: green over red by more than r, over blue
 *                     by more than b and above g; the row at row per cent
 *                     of the height. A wetcut line after the picture's own.
 *                     With 40,20,80,30 at the fish tank it counts the
 *                     vending machine's lit screen in the frame and its
 *                     mirror on the floor below: what of the machine the
 *                     floor's mirror loses as the camera tilts.
 *     -tankcensus    every room's floor squares against the .TRG: the
 *                     art's copy of the room under the floor and how the
 *                     floor over it is drawn (census and censussq lines,
 *                     ProbeCensus). Needs -roombox; -hdwater 0 is enough.
 *     -marblecensus   every room's floor squares by whether they are polished
 *                     stone and would take a mirror: the footstep material,
 *                     the art floor's page, class and pass, the art's copy
 *                     under it (marblesq, marbleroom, marblepage, marblefloor
 *                     and marblelevel lines, ProbeMarbleCensus); -marblequiet
 *                     the same without the marblesq lines. Needs -roombox.
 *     -marblepages a,b,...  the pages a polished floor wears, in place of
 *                     what the data says (MarbleStone).
 *     -marble 0|1     the engine's marble floors (tomb5.hd_marble; off where
 *                     not given): the mirror faces built with the level, the
 *                     frame's rooms, statics and models (-wetlara,
 *                     -wetstand) drawn through each floor height and laid
 *                     over the floor (HDRenderMarbleDraw), timed on the GPU
 *                     (marble: line).
 *     -marbleplanes n the most floor heights a frame mirrors
 *                     (tomb5.hd_marble_planes, 2 where not given).
 *     -marblerows 1|2 the mirror's target at one or two rows a row of the
 *                     frame (2, the engine's, where not given).
 *     -marbleclip <units>  how far over the floor's plane the mirrored draw
 *                     starts (HDRenderMarbleClip; 4, the engine's, where
 *                     not given; 0 puts the clip in the floor, where the
 *                     floor's own triangles blink into its mirror). The
 *                     marble: line ends with the kept faces' spread and
 *                     the faces left out for rising across themselves, and
 *                     one marbleplane: line a plane of the frame follows,
 *                     in its rank: the height, the share of the screen its
 *                     faces cover, drawn or left.
 *     -marblecopies 0|1  1 keeps the art's copy of a room under its
 *                     polished floor in the level (HDRenderMarbleKeepCopies;
 *                     0, the engine's, where not given, leaves it out). The
 *                     marble: line ends with the copy triangles left out and
 *                     of them those in water rooms.
 *     -nowarm         no warm-up of the water's shaders and the marble
 *                     floors' mirror before the picture (HDRenderWaterWarmUp
 *                     and HDRenderMarbleWarmUp, which the engine runs once a
 *                     level and RoomProbe once a picture by default, after
 *                     the depth texture's bind): the first sheet draw then
 *                     waits for the driver as a game's first water frame
 *                     did. The warm-up prints its time per shader.
 *     -rippleat x,y,z  whether a game ripple there loses its classic
 *                     sprite to a sheet's ring (HDRenderWaterSheetUnder);
 *                     repeatable, printed after the WATERLOG lines.
 *     -wader x,z      a stand-in for a wading Lara at world x, z: a box
 *                     768 high on the floor under the sheet there, drawn
 *                     with the creatures' mark, and Lara's place there as
 *                     -lara gives it; repeatable. Needs -hdwater 1.
 *     -swimmer x,z,below
 *                     a stand-in for a body swimming under the surface at
 *                     world x, z: a box 160 across and 200 deep, its top
 *                     `below` units under the sheet there, drawn with the
 *                     creatures' mark; repeatable. Needs -hdwater 1.
 *     -latemark       -creature's stand-ins drawn outside the mark's
 *                     bracket, as the classic path draws a rat, and
 *                     marked before the copy as HDFrameDrawGlow marks
 *                     the rats (HDRenderWaterMarkTriangles).
 *     -lara x,z       Lara's place in the water for the sheets' waterline,
 *                     which leaves out the circle round her that the game
 *                     leaves out round a swimming or wading Lara; world
 *                     x, z. RoomProbe draws no Lara. Needs -hdwater 1.
 *     -foot x1,z1,x2,z2,r
 *                     a waterfall's foot on the sheets along the line from
 *                     world x1, z1 to x2, z2 with reach r - the foam pushed
 *                     away from it and its rings, as a row's `foot x1 z1 x2
 *                     z2 r` gives them; repeatable. -swirl x,z,r is a foot
 *                     from x, z to x, z. The waterfall objects' own feet
 *                     come without it: -hdwater reads the classic .TRC's
 *                     items and meshes as the engine reads items[] and
 *                     prints a WATERLOG line per object, per source and
 *                     per segment of a footprint. RoomProbe draws neither
 *                     the objects nor the sparks, so the drops are not in
 *                     its pictures. Needs -hdwater 1.
 *     -shatter       hide every destructible piece of the drawn room -
 *                     what the room looks like after Lara has shot the
 *                     statics whose art the .TRG baked in.
 *     -smash <room>:<mesh>  the static Lara has shot: the pieces the engine
 *                     pairs with it hidden, each pair written as
 *                     hd_level.cpp's UpdateDestructibles writes it.
 *                     Repeatable. Needs -roombox. With -flip the room is
 *                     the number after those flips, the static the one
 *                     whose list stands there (ANDREA2: -flip 132 -smash
 *                     132:0 is the window the controller breaks).
 *     -smashcensus    every destructible piece, every shatterable static
 *                     (slots 50 to 59) and the pieces within 1024 units of
 *                     it with the gap per axis and the pairing's answer
 *                     (` twin` for a piece of the room's flipmap twin);
 *                     the pieces no static takes and the ones several
 *                     take. Prints and stops. Needs -roombox.
 *     -smashpiece <n> destructible piece n (the "destructible:" list's
 *                     number) hidden in whichever group it lies, the seed
 *                     room's or a -with room's. Repeatable.
 *     -hasgeom <x0 y0 z0 x1 y1 z1>
 *                     does the drawn group draw anything inside that
 *                     room-local box? The question hd_level.cpp asks per
 *                     static mesh; a NO is a static the room seam would
 *                     make vanish, and it is drawn the classic way instead.
 *                     Repeat the switch, up to eight.
 *     -shimmer        the whole room shimmers - the selector aimed at the
 *                     constant fourth flag byte, because a tool with no
 *                     classic level behind it has no per-vertex marks.
 *     -wibble <n>     the engine's wibble counter, which drives both. Two
 *                     runs with two values are two moments in the water.
 *     -paritycheck    run the classic formula and the shader formula over up
 *                     to 200 of the room's own vertices on the processor and
 *                     print the worst difference per channel, then stop.
 *
 *   The texture budget and the small card. These four are the only way to
 *   see the fallbacks work: no machine here has a card that needs them, and
 *   a path nobody has run is a path that does not work.
 *
 *     -budget <MiB>   force a texture budget. Over it the largest pages give
 *                     up their largest mip level, biggest first, until what
 *                     is left fits - never a whole page. 0 means no cap,
 *                     which is what the tool does by default.
 *     -format dxt5    hold the pages as DXT5 instead of A8R8G8B8: a quarter
 *                     of the video memory, a measurable loss of quality.
 *                     `-format rgba` is the default and says so out loud.
 *     -psnr           decode one page both ways on the processor and print
 *                     the peak signal to noise ratio of DXT5 against the
 *                     lossless original, per channel and over all four.
 *     -caps maxtex <n>  pretend the device holds no texture wider than n
 *     -caps noidx32     pretend the device has no 32 bit index buffer, so
 *                     every draw call goes through a 16 bit index and a moved
 *                     vertex stream
 *     -seq <A,B,C>    build those levels one after the other, printing commit
 *                     and free texture memory between each - the proof that a
 *                     level change never holds two levels at once.
 *     -vramhog        fill the card's texture memory with 64 MiB blocks until
 *                     it will take no more, THEN load the level. That is the
 *                     one way to see what happens when CreateTexture fails in
 *                     the middle of a level: the pages stop, the reason is
 *                     said, and the whole level goes back to classic instead
 *                     of half of it drawing without textures.
 *
 *     -devicecycle    the run three times on three devices, as Alt+Enter
 *                     does it in the game: the picture with every switch
 *                     given (INTZ bound), then HDRenderShutdown with the
 *                     device alive - the HD half of HDLevelDeviceLost - and
 *                     the device released (its reference count printed: 0
 *                     is no object of ours left on it); a new device draws
 *                     the classic picture (-classicroom <room> -classiconly
 *                     added); a third builds the level again and draws the
 *                     first picture once more. _cycle1, _cycle2 and _cycle3
 *                     after the -out prefix; the first and the third are
 *                     compared byte for byte.
 *     -cycleclassic <pages>
 *                     the classic draw across Alt+Enter, no level needed:
 *                     <pages> MANAGED 256 x 256 pages made the way
 *                     CreateTexturePage makes them and drawn the way
 *                     HWR_DrawSortList draws (the engine's FVF, the stage
 *                     state HWInitialise sets, DrawPrimitiveUP), then the
 *                     device released and another made, the pages moved by
 *                     the engine's own specific/devicemove.h and drawn
 *                     again, and once more on a third device (a second
 *                     toggle). _classic1.._classic3 after the -out prefix,
 *                     compared byte for byte. -nomove leaves the pages on
 *                     the released device: the crash of Alt+Enter, its CRASH
 *                     line through the crash seam and a dump in the working
 *                     folder.
 *     -windowcycle [-leak]
 *                     the game window across Alt+Enter, no level needed: a
 *                     real window of the engine's class and styles through
 *                     the engine's own specific/devicewindow.h, the start
 *                     state and two toggles, each state's line (styles,
 *                     rectangles, the present parameters the device
 *                     reports, its creation flags), what a read of the
 *                     screen and the DXGI desktop duplication see there, and
 *                     the screenshot path's HRESULT and file
 *                     (_state1.._state3.tga after the -out prefix). The
 *                     first and the third state's lines are compared. -leak
 *                     keeps a texture of the start device alive. The window
 *                     covers the monitor for about two seconds.
 *     -crash <folder> the crash seam (specific/crashseam.h): the tool runs
 *                     itself in <folder> with the seam installed and a null
 *                     pointer read, then reads the CRASH line of crash.txt
 *                     back and checks the minidump beside it (MDMP header).
 *     -hddrawn 0|1    the HD level built, drawn HD (1, the default) or
 *                     classic (0), as the tool's HDDrawn() answers after the
 *                     switch's own press rule (hd_switch.h): while it answers
 *                     false the classic faces of the seed room and of
 *                     every -with room in place of the art, as -classicroom
 *                     for each with -classiconly. Needs -roombox. The faces
 *                     come from the .TRC, not from the build, so the
 *                     picture is that of the explicit list.
 *
 * Everything outside the pixels goes to stdout, so a run can be pasted into a
 * report as it stands.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <d3d9.h>

#include <math.h>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>
#include <set>

#include "zlib.h"

/* By path, not through a /I of specific\: that folder holds the engine's own
 * time.h, and putting it on the include path makes <chrono> pick it up
 * instead of the CRT's and fail with two hundred lines about asctime. A
 * quoted include is resolved from THIS file's folder first, so this works
 * from both build scripts. */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_render.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_water_under.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_pagecache.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_splash_math.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_light.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_dxt5.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_scan.h"
/* The .hdm sidecar reader - the engine's own, not a copy of it. The tool
 * has to answer a version 2 file the way the game does or its pictures
 * cannot be compared with the game's. */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_markers.h"
/* The object path for -life - the engine's own, as ModelProbe links it -
 * and the water's life's rules, the engine's own as well. */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_model.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_life_rules.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_spring.h"
#include <algorithm>
/* hd_install.cpp reads the two registry overrides out of these, which the
 * engine defines in tomb5\tomb5.cpp; ModelProbe brings the same pair. Left
 * empty, so the search runs as it does for a player who never set them. */
char tomb5_hd_install[260];
char tomb5_hd_install_tr3[260];
#include "../../../TOMB5 Code/TOMB5/specific/bc7.h"
#include "../../../TOMB5 Code/TOMB5/specific/crashseam.h"
#include "../../../TOMB5 Code/TOMB5/specific/devicemove.h"
/* The randomizer's own reader for the CLASSIC level, so that the water
 * survey can hold the level's own faces against the .TRG's art through the
 * engine's own gate. Nothing else in this tool needs a classic level. */
#include "core/Tr5Level.h"
// The visibility path's tables and their classic side (-vis).
#include "VisInput.h"
/* The vendored .TRG reader, for the one thing the walk asks of the art
 * without building it: which groups have any triangle (walkoff::LoadHd). */
#include "trf_remaster/Trg.h"
#include <string>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "psapi.lib")

/* ------------------------------------------------------------------ PNG -- */
/* The same writer DdsProbe uses: 24 bit truecolour, filter 0 on every
 * scanline, deflate through the zlib the engine already links. Small and
 * obviously correct beats small. */

static void PutBE(unsigned char* p, unsigned long v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
}

static void PngChunk(FILE* f, const char* tag, const unsigned char* data, unsigned long len)
{
	unsigned char head[8];
	unsigned char tail[4];
	unsigned long crc;

	PutBE(head, len);
	memcpy(head + 4, tag, 4);
	fwrite(head, 1, 8, f);

	if (len)
		fwrite(data, 1, len, f);

	crc = crc32(0, (const Bytef*)tag, 4);

	if (len)
		crc = crc32(crc, (const Bytef*)data, len);

	PutBE(tail, crc);
	fwrite(tail, 1, 4, f);
}

static bool WritePng(const char* path, const unsigned long* px, long w, long h)
{
	FILE* f;
	unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	unsigned char ihdr[13];
	unsigned char* raw;
	unsigned char* zbuf;
	unsigned long rawLen, zLen;
	long x, y;
	unsigned char* q;

	rawLen = (unsigned long)h * (1 + (unsigned long)w * 3);
	raw = (unsigned char*)malloc(rawLen);

	if (!raw)
		return false;

	q = raw;

	for (y = 0; y < h; y++)
	{
		*q++ = 0;

		for (x = 0; x < w; x++)
		{
			const unsigned long c = px[y * w + x];
			*q++ = (unsigned char)(c >> 16);
			*q++ = (unsigned char)(c >> 8);
			*q++ = (unsigned char)c;
		}
	}

	zLen = compressBound(rawLen);
	zbuf = (unsigned char*)malloc(zLen);

	if (!zbuf || compress2(zbuf, &zLen, raw, rawLen, 6) != Z_OK)
	{
		free(raw);
		free(zbuf);
		return false;
	}

	free(raw);
	f = fopen(path, "wb");

	if (!f)
	{
		free(zbuf);
		return false;
	}

	fwrite(sig, 1, 8, f);
	PutBE(ihdr, (unsigned long)w);
	PutBE(ihdr + 4, (unsigned long)h);
	ihdr[8] = 8;		// bit depth
	ihdr[9] = 2;		// truecolour
	ihdr[10] = 0;
	ihdr[11] = 0;
	ihdr[12] = 0;
	PngChunk(f, "IHDR", ihdr, 13);
	PngChunk(f, "IDAT", zbuf, zLen);
	PngChunk(f, "IEND", 0, 0);
	fclose(f);
	free(zbuf);
	return true;
}

/* --------------------------------------------------------------- camera -- */

/* The engine's own view convention (3dmath.cpp, drawroom.cpp): x right,
 * y DOWN, z forward, and a point goes through the matrix as a ROW vector.
 * yaw 0 looks along +Z and grows towards +X - the same sense as a TR5 heading
 * angle - and a positive pitch looks downwards, because down is +y here. */
static void LookAt(double ex, double ey, double ez, double yawDeg, double pitchDeg,
	float* out)
{
	const double d2r = 3.14159265358979323846 / 180.0;
	const double yaw = yawDeg * d2r;
	const double pitch = pitchDeg * d2r;

	const double sy = sin(yaw), cy = cos(yaw);
	const double sp = sin(pitch), cp = cos(pitch);

	// forward, right, and down = forward x right (a proper basis: with
	// pitch 0 it comes out as world +y, which is world down).
	const double f[3] = { sy * cp, sp, cy * cp };
	const double r[3] = { cy, 0.0, -sy };
	const double d[3] =
	{
		f[1] * r[2] - f[2] * r[1],
		f[2] * r[0] - f[0] * r[2],
		f[0] * r[1] - f[1] * r[0]
	};

	out[0] = (float)r[0];	out[1] = (float)d[0];	out[2] = (float)f[0];	out[3] = 0;
	out[4] = (float)r[1];	out[5] = (float)d[1];	out[6] = (float)f[1];	out[7] = 0;
	out[8] = (float)r[2];	out[9] = (float)d[2];	out[10] = (float)f[2];	out[11] = 0;
	out[12] = (float)-(ex * r[0] + ey * r[1] + ez * r[2]);
	out[13] = (float)-(ex * d[0] + ey * d[1] + ez * d[2]);
	out[14] = (float)-(ex * f[0] + ey * f[1] + ez * f[2]);
	out[15] = 1;
}

/* AlterFOV (3dmath.cpp), without the engine: the field of view is
 * measured against a 4:3 width whatever the window really is, which is why a
 * 16:9 picture is horizontally wider and not vertically shorter. Reproduced
 * rather than approximated, so the probe frames a room the way the game
 * will. */
static double EnginePersp(long height, double fovDeg)
{
	const double d2r = 3.14159265358979323846 / 180.0;
	const long fovWidth = height * 320 / 240;
	const double half = fovDeg * 0.5 * d2r;

	return (double)(fovWidth / 2) * cos(half) / sin(half);
}

/* The engine's own camera, integer angles and all (specific\3dmath.cpp).
 *
 * phd_LookAt turns an eye and a target into two angles with
 * phd_GetVectorAngles - phd_atan over the octant table phdtan2 and
 * phdtantab, and phd_sqrt - and aGenerateW2V builds the float rotation the
 * portal walk projects with out of fcossin_tbl, a float sine table of 65536
 * steps. AlterFOV's perspective is integer arithmetic on rcossin_tbl. Worked
 * out in doubles instead, as LookAt and EnginePersp above do, a portal corner
 * lands up to a pixel away, a window comes out a pixel wider or narrower,
 * and a portal sliver at the rim of the screen lists a room the engine does
 * not list. So the walk goes through these, and -walktarget hands their
 * angles and perspective to the picture as well.
 *
 * The same tables as the engine's, out of the same header, and the float
 * table filled by the same expression as aInitMatrix. */
#include "../../../TOMB5 Code/TOMB5/global/math_tbls.h"

namespace enginecam
{
	static float g_fsin[65536];
	static bool g_ready = false;

	static void Init()
	{
		if (g_ready)
			return;

		for (int i = 0; i < 65536; i++)
		{
			const float ang = (float)i * float(3.14159265358979323846 * 2.0F / 65536.0F);

			g_fsin[i] = sin(ang);
		}

		g_ready = true;
	}

	static short PhdSin(long angle)
	{
		angle >>= 3;
		return (short)(4 * rcossin_tbl[angle & 0x1FFE]);
	}

	static short PhdCos(long angle)
	{
		angle >>= 3;
		return (short)(4 * rcossin_tbl[(angle & 0x1FFE) + 1]);
	}

	static float FSin(long angle)
	{
		return g_fsin[(unsigned short)angle];
	}

	static float FCos(long angle)
	{
		return g_fsin[(unsigned short)(angle + 0x4000)];
	}

	// phd_atan.
	static long Atan(long x, long y)
	{
		long octant = 0, n, result = 0;

		if (x || y)
		{
			if (x < 0)
			{
				octant += 4;
				x = -x;
			}

			if (y < 0)
			{
				octant += 2;
				y = -y;
			}

			if (y > x)
			{
				octant++;
				n = x;
				x = y;
				y = n;
			}

			while ((short)y != y)
			{
				x >>= 1;
				y >>= 1;
			}

			result = phdtan2[octant] + phdtantab[(y << 11) / x];

			if (result < 0)
				result = -result;
		}

		return result;
	}

	// phd_sqrt.
	static unsigned long Sqrt(unsigned long num)
	{
		unsigned long base = 0x40000000, result = 0, tmp;

		do
		{
			tmp = result;
			result += base;
			tmp >>= 1;

			if (result > num)
				result = tmp;
			else
			{
				num -= result;
				result = base | tmp;
			}

			base >>= 2;
		} while (base);

		return result;
	}

	/* phd_GetVectorAngles: [0] the heading (y_rot), [1] the pitch (x_rot),
	 * negative when the target lies below the eye. */
	static void VectorAngles(long x, long y, long z, short* angles)
	{
		short atan;

		angles[0] = (short)Atan(z, x);

		while ((short)x != x || (short)y != y || (short)z != z)
		{
			x >>= 2;
			y >>= 2;
			z >>= 2;
		}

		atan = (short)Atan((long)Sqrt((unsigned long)(z * z + x * x)), y);

		if ((y > 0 && atan > 0) || (y < 0 && atan < 0))
			atan = -atan;

		angles[1] = atan;
	}

	/* aGenerateW2V's rotation, rows first as aW2VMx holds it: M00..M02,
	 * M10..M12, M20..M22. The poison sway the engine scales it by is not
	 * here - no log line carries it. */
	static void Rotation(short xrot, short yrot, short zrot, float* m)
	{
		const float sx = FSin(xrot), cx = FCos(xrot);
		const float sy = FSin(yrot), cy = FCos(yrot);
		const float sz = FSin(zrot), cz = FCos(zrot);

		m[0] = sx * sy * sz + cy * cz;
		m[1] = cx * sz;
		m[2] = sx * cy * sz - sy * cz;
		m[3] = sx * sy * cz - cy * sz;
		m[4] = cx * cz;
		m[5] = sx * cy * cz + sy * sz;
		m[6] = cx * sy;
		m[7] = -sx;
		m[8] = cx * cy;
	}

	// AlterFOV's phd_persp, for a field of view in the engine's units.
	static long Persp(long height, short fov)
	{
		fov /= 2;

		const long fovWidth = height * 320 / 240;

		return (fovWidth / 2) * PhdCos(fov) / PhdSin(fov);
	}

	/* A field of view in degrees in the engine's units, the way InitWindow
	 * hands the configured one to AlterFOV: 80 degrees is 14560. */
	static short FovUnits(double degrees)
	{
		return (short)(182 * (long)(degrees + 0.5));
	}

	/* Degrees in the engine's units, for -walkyaw and -walkpitch, which name
	 * a direction the engine never computes and so are rounded once. */
	static short Angle(double degrees)
	{
		const long units = (long)floor(degrees * 65536.0 / 360.0 + 0.5);

		return (short)(unsigned short)(units & 0xFFFF);
	}
}

/* ------------------------------------------------------------------ mem -- */

static double PeakCommitMiB()
{
	PROCESS_MEMORY_COUNTERS_EX counters;

	memset(&counters, 0, sizeof(counters));
	counters.cb = sizeof(counters);

	if (!GetProcessMemoryInfo(GetCurrentProcess(),
		(PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters)))
		return 0.0;

	return (double)counters.PeakPagefileUsage / 1048576.0;
}

static double CommitMiB()
{
	PROCESS_MEMORY_COUNTERS_EX counters;

	memset(&counters, 0, sizeof(counters));
	counters.cb = sizeof(counters);

	if (!GetProcessMemoryInfo(GetCurrentProcess(),
		(PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters)))
		return 0.0;

	return (double)counters.PagefileUsage / 1048576.0;
}

/* ------------------------------------------------------------------ d3d -- */

static HWND g_window = 0;
static IDirect3D9* g_d3d = 0;
static IDirect3DDevice9* g_device = 0;

static bool MakeDevice()
{
	WNDCLASSA cls;
	D3DPRESENT_PARAMETERS pp;

	memset(&cls, 0, sizeof(cls));
	cls.lpfnWndProc = DefWindowProcA;
	cls.hInstance = GetModuleHandleA(0);
	cls.lpszClassName = "RoomProbeWindow";
	RegisterClassA(&cls);

	// Never shown: WS_POPUP without WS_VISIBLE, one pixel off screen. The
	// device still needs a real HWND, a hidden one is enough.
	g_window = CreateWindowA("RoomProbeWindow", "RoomProbe", WS_POPUP,
		-32, -32, 16, 16, 0, 0, cls.hInstance, 0);

	if (!g_window)
	{
		printf("RoomProbe: CreateWindow failed\n");
		return false;
	}

	g_d3d = Direct3DCreate9(D3D_SDK_VERSION);

	if (!g_d3d)
	{
		printf("RoomProbe: Direct3DCreate9 failed\n");
		return false;
	}

	memset(&pp, 0, sizeof(pp));
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = D3DFMT_X8R8G8B8;
	pp.BackBufferWidth = 16;
	pp.BackBufferHeight = 16;
	pp.hDeviceWindow = g_window;
	pp.EnableAutoDepthStencil = FALSE;

	if (FAILED(g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_window,
		D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &g_device)))
	{
		printf("RoomProbe: CreateDevice(HAL, hardware vertex processing) failed\n");
		return false;
	}

	return true;
}

// -devicecycle: say how many references the released device still had.
static bool g_cycle_report = false;

static void DropDevice()
{
	if (g_device)
	{
		const ULONG left = g_device->Release();

		if (g_cycle_report)
			printf("devicecycle: device released, RefCnt = %lu\n",
				(unsigned long)left);

		g_device = 0;
	}

	if (g_d3d) { g_d3d->Release(); g_d3d = 0; }
	if (g_window) { DestroyWindow(g_window); g_window = 0; }
}

/* --------------------------------------------------------------- proofs -- */

static void PrintZProof(double persp)
{
	HDRenderZSample rows[50];
	double a = 0, b = 0, boo = 0;

	HDRenderZConstants(20.0, 20480.0, &a, &b, &boo);

	const double worst = HDRenderSelfTest(20.0, 20480.0, persp, rows, 50);

	printf("\n-- the depth proof ------------------------------------------\n");
	printf("SetupZRange(20, 20480): f_a %.9f  f_b %.9f  f_boo %.9f\n", a, b, boo);
	printf("projection: _33 %.9f  _43 %.9f  _34 1  _44 0\n", a, -b);
	printf("\n  %12s %14s %14s %12s\n", "z_view", "classic sz", "matrix depth", "difference");

	// Five of the fifty, spread over the range: near plane, the two ends of
	// the middle, and the far plane.
	const int show[5] = { 0, 12, 24, 37, 49 };

	for (int i = 0; i < 5; i++)
	{
		const HDRenderZSample& row = rows[show[i]];
		printf("  %12.3f %14.9f %14.9f %12.2e\n",
			row.zView, row.classic, row.matrix, row.diff);
	}

	printf("\nworst of 50 samples: %.3e  (%s)\n", worst,
		worst < 1e-5 ? "PASS - the two paths write the same depth"
			: "FAIL - the shared depth buffer does not hold");
}

static void PrintDepthTruth(const char* when)
{
	unsigned long zenable = 0, zfunc = 0, zwrite = 0;
	float minZ = 0, maxZ = 0;

	HDRenderDepthTruth(g_device, &zenable, &zfunc, &zwrite, &minZ, &maxZ);

	const char* zname = zenable == 0 ? "D3DZB_FALSE"
		: zenable == 1 ? "D3DZB_TRUE"
		: zenable == 2 ? "D3DZB_USEW" : "?";

	static const char* funcs[9] =
	{
		"?", "NEVER", "LESS", "EQUAL", "LESSEQUAL",
		"GREATER", "NOTEQUAL", "GREATEREQUAL", "ALWAYS"
	};

	printf("device depth truth %-14s ZENABLE %lu (%s)  ZFUNC %lu (%s)  "
		"ZWRITE %lu  viewport z %.3f..%.3f\n",
		when, zenable, zname, zfunc,
		zfunc < 9 ? funcs[zfunc] : "?", zwrite, minZ, maxZ);
}

/* ------------------------------------------------------- state bracket -- */
/*
 * HDRenderFrameBegin / HDRenderFrameEnd promise that the device comes out of
 * an HD block in exactly the state it went in - the classic fixed-function
 * path draws straight after it and would show the difference as garbage. A
 * promise is not a proof, so this reads the device twice.
 *
 * The list is deliberately WIDER than the one hd_render saves: every render
 * state the classic path is known to set (function_table.cpp,
 * polyinsert.cpp per bucket, LoadSave.cpp for the filter switch), plus a few
 * it never touches, so that a state hd_render changes and forgets to save is
 * caught here rather than in the game.
 */
static const D3DRENDERSTATETYPE kWatchedStates[] =
{
	D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_FILLMODE,
	D3DRS_SHADEMODE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC,
	D3DRS_DITHERENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
	D3DRS_BLENDOP, D3DRS_CULLMODE, D3DRS_FOGENABLE, D3DRS_SPECULARENABLE,
	D3DRS_LIGHTING, D3DRS_COLORVERTEX, D3DRS_AMBIENT, D3DRS_CLIPPING,
	D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_COLORWRITEENABLE,
	D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRGBWRITEENABLE,
	D3DRS_MULTISAMPLEANTIALIAS, D3DRS_TEXTUREFACTOR, D3DRS_WRAP0,
	D3DRS_NORMALIZENORMALS, D3DRS_LOCALVIEWER, D3DRS_DEPTHBIAS,
	D3DRS_SLOPESCALEDEPTHBIAS
};

static const D3DSAMPLERSTATETYPE kWatchedSamplers[] =
{
	D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_ADDRESSW, D3DSAMP_BORDERCOLOR,
	D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER,
	D3DSAMP_MIPMAPLODBIAS, D3DSAMP_MAXMIPLEVEL, D3DSAMP_MAXANISOTROPY,
	D3DSAMP_SRGBTEXTURE
};

static const D3DTEXTURESTAGESTATETYPE kWatchedStages[] =
{
	D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2,
	D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2, D3DTSS_TEXCOORDINDEX
};

static const int kStateCount = (int)(sizeof(kWatchedStates) / sizeof(kWatchedStates[0]));
static const int kSamplerCount = (int)(sizeof(kWatchedSamplers) / sizeof(kWatchedSamplers[0]));
static const int kStageCount = (int)(sizeof(kWatchedStages) / sizeof(kWatchedStages[0]));

struct Snapshot
{
	DWORD rs[kStateCount];
	/* Samplers 0 to 2: the page's, and the light field's two, which the
	 * room bracket binds for the gloss variant and has to put back. */
	DWORD samp[3][kSamplerCount];
	void* fieldTexture[2];
	DWORD stage[kStageCount];
	/* The scissor rectangle. D3DRS_SCISSORTESTENABLE is in the render state
	 * list above; the rectangle beside it is a device state of its own, and
	 * the HD path writes it once per room for the portal window. */
	RECT scissor;
	BOOL scissorOk;
	DWORD fvf;
	void* vs;
	void* ps;
	void* decl;
	void* stream;
	void* indices;
	void* texture;
	UINT offset, stride;
};

static void Take(Snapshot& shot)
{
	memset(&shot, 0, sizeof(shot));

	for (int i = 0; i < kStateCount; i++)
		g_device->GetRenderState(kWatchedStates[i], &shot.rs[i]);
	for (int s = 0; s < 3; s++)
		for (int i = 0; i < kSamplerCount; i++)
			g_device->GetSamplerState((DWORD)s, kWatchedSamplers[i], &shot.samp[s][i]);
	for (int s = 0; s < 2; s++)
	{
		IDirect3DBaseTexture9* bound = 0;

		g_device->GetTexture((DWORD)(1 + s), &bound);
		shot.fieldTexture[s] = bound;

		if (bound)
			bound->Release();
	}
	for (int i = 0; i < kStageCount; i++)
		g_device->GetTextureStageState(0, kWatchedStages[i], &shot.stage[i]);

	// Group M: the rectangle. A device with no scissor cap fails the Get -
	// then the flag is FALSE in both snapshots and they still compare equal.
	shot.scissorOk = SUCCEEDED(g_device->GetScissorRect(&shot.scissor));

	g_device->GetFVF(&shot.fvf);

	// The Get* calls add a reference; take the pointer as an identity and
	// give the reference straight back, so the comparison cannot itself
	// change what it measures.
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DVertexBuffer9* stream = 0;
	IDirect3DIndexBuffer9* indices = 0;
	IDirect3DBaseTexture9* texture = 0;

	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetStreamSource(0, &stream, &shot.offset, &shot.stride);
	g_device->GetIndices(&indices);
	g_device->GetTexture(0, &texture);

	shot.vs = vs; shot.ps = ps; shot.decl = decl;
	shot.stream = stream; shot.indices = indices; shot.texture = texture;

	if (vs) vs->Release();
	if (ps) ps->Release();
	if (decl) decl->Release();
	if (stream) stream->Release();
	if (indices) indices->Release();
	if (texture) texture->Release();
}

static int Diff(const Snapshot& before, const Snapshot& after)
{
	int broken = 0;

	for (int i = 0; i < kStateCount; i++)
		if (before.rs[i] != after.rs[i])
		{
			printf("  BROKEN render state %d: %lu -> %lu\n",
				(int)kWatchedStates[i], before.rs[i], after.rs[i]);
			broken++;
		}

	for (int s = 0; s < 3; s++)
		for (int i = 0; i < kSamplerCount; i++)
			if (before.samp[s][i] != after.samp[s][i])
			{
				printf("  BROKEN sampler %d state %d: %lu -> %lu\n", s,
					(int)kWatchedSamplers[i], before.samp[s][i], after.samp[s][i]);
				broken++;
			}

	for (int s = 0; s < 2; s++)
		if (before.fieldTexture[s] != after.fieldTexture[s])
		{
			printf("  BROKEN texture %d: %p -> %p\n", 1 + s, before.fieldTexture[s],
				after.fieldTexture[s]);
			broken++;
		}

	for (int i = 0; i < kStageCount; i++)
		if (before.stage[i] != after.stage[i])
		{
			printf("  BROKEN texture stage 0 state %d: %lu -> %lu\n",
				(int)kWatchedStages[i], before.stage[i], after.stage[i]);
			broken++;
		}

	struct { const char* name; const void* a; const void* b; } objects[] =
	{
		{ "vertex shader", before.vs, after.vs },
		{ "pixel shader", before.ps, after.ps },
		{ "vertex declaration", before.decl, after.decl },
		{ "stream 0", before.stream, after.stream },
		{ "index buffer", before.indices, after.indices },
		{ "texture 0", before.texture, after.texture }
	};

	for (int i = 0; i < 6; i++)
		if (objects[i].a != objects[i].b)
		{
			printf("  BROKEN %s: %p -> %p\n",
				objects[i].name, objects[i].a, objects[i].b);
			broken++;
		}

	if (before.fvf != after.fvf)
	{
		printf("  BROKEN FVF: 0x%08lX -> 0x%08lX\n", before.fvf, after.fvf);
		broken++;
	}

	// Group M: the scissor rectangle, which the HD room path now sets per
	// room and hd_render's bracket has to put back with everything else.
	if (before.scissorOk != after.scissorOk ||
		(before.scissorOk &&
			(before.scissor.left != after.scissor.left ||
			before.scissor.top != after.scissor.top ||
			before.scissor.right != after.scissor.right ||
			before.scissor.bottom != after.scissor.bottom)))
	{
		printf("  BROKEN scissor rect: %ld,%ld,%ld,%ld -> %ld,%ld,%ld,%ld\n",
			before.scissor.left, before.scissor.top,
			before.scissor.right, before.scissor.bottom,
			after.scissor.left, after.scissor.top,
			after.scissor.right, after.scissor.bottom);
		broken++;
	}

	/* Offset and stride only mean anything while a buffer is bound. With
	 * stream 0 unbound Direct3D 9 keeps the last stride it was given -
	 * SetStreamSource(0, NULL, 0, 0) is accepted and GetStreamSource still
	 * answers 20 - so an unbound-before / unbound-after pair is the same
	 * state however the two strides read. Measured here, not assumed: this
	 * check reported it before it was written down. */
	if (before.stream && after.stream &&
		(before.offset != after.offset || before.stride != after.stride))
	{
		printf("  BROKEN stream 0 offset/stride: %u/%u -> %u/%u\n",
			before.offset, before.stride, after.offset, after.stride);
		broken++;
	}
	else if (!before.stream && before.stride != after.stride)
		printf("  (stream 0 unbound, stride reads %u instead of %u - "
			"Direct3D keeps the last one and nothing can read it)\n",
			after.stride, before.stride);

	return broken;
}

/* The device as the classic path leaves it (function_table.cpp), so
 * that the bracket is tested against the real caller's state and not against
 * Direct3D's fresh defaults - the two are not the same in a single one of
 * these lines. */
static void PoseAsClassicPath()
{
	g_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	g_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	g_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	g_device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU);

	g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
	g_device->SetRenderState(D3DRS_SPECULARENABLE, 1);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, 1);
	g_device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	g_device->SetRenderState(D3DRS_DITHERENABLE, 1);
	g_device->SetRenderState(D3DRS_ALPHAREF, 0);
	g_device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_NOTEQUAL);
	g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, 0);
	g_device->SetRenderState(D3DRS_AMBIENT, 0);
	g_device->SetRenderState(D3DRS_COLORVERTEX, 0);
	g_device->SetRenderState(D3DRS_FOGENABLE, 0);
	g_device->SetRenderState(D3DRS_CLIPPING, FALSE);
	g_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2);
}

/* ---------------------------------------------------------------- draw -- */

struct Shot
{
	IDirect3DTexture9* target = 0;
	IDirect3DSurface9* colour = 0;
	IDirect3DSurface9* depth = 0;
	IDirect3DSurface9* readback = 0;
	IDirect3DSurface9* oldColour = 0;
	long w = 0, h = 0;
};

// -black: clear the render target to black instead of magenta - see the note
// in ShotBegin.
static bool g_black = false;
// -glow: the additive pass alone over black, with and without the scene's
// depth - see GlowShots.
static bool g_glow_alone = false;
// -nocopy, -nodepth, -waterdepthview and -waterbend - see the usage above.
static bool g_water_copy = true;
static bool g_water_depth = true;
// -nowarm: no warm-up of the water's shaders (HDRenderWaterWarmUp).
static bool g_nowarm = false;
static int g_water_depth_view = 0;
static float g_water_bend = 1.0f;

static bool ShotBegin(Shot& shot, long w, long h)
{
	shot.w = w;
	shot.h = h;

	if (FAILED(g_device->CreateTexture((UINT)w, (UINT)h, 1, D3DUSAGE_RENDERTARGET,
		D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &shot.target, 0)))
	{
		printf("RoomProbe: CreateTexture(render target %ldx%ld) failed\n", w, h);
		return false;
	}

	if (FAILED(shot.target->GetSurfaceLevel(0, &shot.colour)))
		return false;

	if (FAILED(g_device->CreateDepthStencilSurface((UINT)w, (UINT)h, D3DFMT_D24S8,
		D3DMULTISAMPLE_NONE, 0, TRUE, &shot.depth, 0)))
	{
		printf("RoomProbe: CreateDepthStencilSurface failed\n");
		return false;
	}

	if (FAILED(g_device->CreateOffscreenPlainSurface((UINT)w, (UINT)h,
		D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &shot.readback, 0)))
	{
		printf("RoomProbe: CreateOffscreenPlainSurface failed\n");
		return false;
	}

	g_device->GetRenderTarget(0, &shot.oldColour);
	g_device->SetRenderTarget(0, shot.colour);
	g_device->SetDepthStencilSurface(shot.depth);

	/* The INTZ texture in the D24S8 surface's place, as the engine binds it
	 * for a level with sheets - here for every picture, -hdwater or not,
	 * unless -nodepth; ShotEnd puts the surface back. */
	{
		const char* refused = "ok";

		if (g_water_depth && !HDRenderWaterDepthBind(&refused))
			printf("water depth texture refused: %s\n", refused);
	}

	/* The water's shaders warmed once, as the engine does it at the
	 * level's load once the depth texture is bound (hd_level.cpp,
	 * BuildWaterSheets); -nowarm leaves it out, for the first draw's wait
	 * it takes away. */
	if (!g_nowarm)
	{
		double ms[10];
		const double all = HDRenderWaterWarmUp(ms, 10);

		if (all >= 0.0)
			printf("-hdwater: warm-up %.2f ms: sheet %.2f, depth %.2f, under "
				"%.2f, foot %.2f, river %.2f, foam %.2f, foam depth %.2f, "
				"cards %.2f, wet floor %.2f, wet models %.2f\n", all, ms[0],
				ms[1], ms[2], ms[3], ms[4], ms[5], ms[6], ms[7], ms[8], ms[9]);

		// The marble floors' mirror, as the engine warms it a level.
		const double marble = HDRenderMarbleWarmUp(ms, 5);

		if (marble >= 0.0)
			printf("-marble: warm-up %.2f ms: target %.2f, plain %.2f, "
				"gloss %.2f, glow %.2f, layer %.2f\n", marble, ms[0], ms[1],
				ms[2], ms[3], ms[4]);
	}

	/* Magenta, not black. A room that draws nothing and a room that draws
	 * black are the same picture on a black background, and the first is a
	 * bug while the second is a dark corner. Anything still magenta in the
	 * result is the background showing through.
	 *
	 * -black turns it off, for the one measurement that needs it: the
	 * additive pass on its own is blended ONE / ONE over whatever is already
	 * there, so on magenta every covered pixel starts at 170 of 255 and "how
	 * much of white is this glow" cannot be read out of the picture at all.
	 * On black it can. */
	g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
		g_black ? D3DCOLOR_XRGB(0, 0, 0) : D3DCOLOR_XRGB(255, 0, 255),
		1.0f, 0);

	return true;
}

// -wetstep: the segment the picture's luma is read along, in pixels.
static bool g_wet_step = false;
static int g_wet_step_at[4] = { 0, 0, 0, 0 };

/* -wetstep: the luma along the segment, one sample a pixel of its longer
 * side, the samples off the picture left out; and the largest step between
 * two neighbouring samples, where it lies. */
static void WetStepLine(const unsigned long* px, long w, long h)
{
	if (!g_wet_step)
		return;

	const int* s = g_wet_step_at;
	const int dx = s[2] - s[0], dy = s[3] - s[1];
	const int n = (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) + 1;
	int last = -1, step = 0, stepX = s[0], stepY = s[1], low = 255, high = 0;
	std::string list;

	for (int i = 0; i < n; i++)
	{
		const float t = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
		const long x = s[0] + (long)lroundf((float)dx * t);
		const long y = s[1] + (long)lroundf((float)dy * t);

		if (x < 0 || y < 0 || x >= w || y >= h)
			continue;

		const unsigned long c = px[y * w + x];
		const int l = (int)lroundf(0.299f * (float)((c >> 16) & 255) +
			0.587f * (float)((c >> 8) & 255) + 0.114f * (float)(c & 255));
		char one[8];

		snprintf(one, sizeof(one), list.empty() ? "%d" : " %d", l);
		list += one;
		low = l < low ? l : low;
		high = l > high ? l : high;

		if (last >= 0 && abs(l - last) > step)
		{
			step = abs(l - last);
			stepX = (int)x;
			stepY = (int)y;
		}

		last = l;
	}

	printf("wetstep: %d,%d to %d,%d samples %d luma %d..%d largest step %d at "
		"%d,%d: %s\n", s[0], s[1], s[2], s[3], n, low, high, step, stepX,
		stepY, list.c_str());
}

// -wetcut: the colour's margins (green over red, over blue, green's least)
// and the row that splits the frame, in per cent of its height.
static bool g_wet_cut = false;
static int g_wet_cut_key[4] = { 0, 0, 0, 0 };

/* -wetcut: the pixels of the keyed colour above the row (the thing itself in
 * the frame) and below it (its mirror on the floor), so a mirror that drops
 * the thing as the camera moves shows as a falling count below. */
static void WetCutLine(const unsigned long* px, long w, long h)
{
	if (!g_wet_cut)
		return;

	const int* k = g_wet_cut_key;
	const long row = h * (long)k[3] / 100;
	long above = 0, below = 0;

	for (long y = 0; y < h; y++)
		for (long x = 0; x < w; x++)
		{
			const unsigned long c = px[y * w + x];
			const int r = (int)((c >> 16) & 255), g = (int)((c >> 8) & 255);
			const int b = (int)(c & 255);

			if (g - r > k[0] && g - b > k[1] && g > k[2])
				(y < row ? above : below)++;
		}

	printf("wetcut: key %d,%d,%d row %ld of %ld above %ld below %ld\n", k[0],
		k[1], k[2], row, h, above, below);
}

static bool ShotWrite(Shot& shot, const char* path)
{
	D3DLOCKED_RECT rect;

	if (FAILED(g_device->GetRenderTargetData(shot.colour, shot.readback)))
	{
		printf("RoomProbe: GetRenderTargetData failed\n");
		return false;
	}

	if (FAILED(shot.readback->LockRect(&rect, 0, D3DLOCK_READONLY)))
	{
		printf("RoomProbe: locking the readback surface failed\n");
		return false;
	}

	unsigned long* pixels = (unsigned long*)malloc((size_t)shot.w * (size_t)shot.h * 4);

	if (!pixels)
	{
		shot.readback->UnlockRect();
		return false;
	}

	// How much of the picture is still the background - the one number that
	// separates "nothing was drawn" from "something dark was drawn".
	long background = 0;
	double sum = 0;
	double peak = 0;
	long long white = 0;

	for (long y = 0; y < shot.h; y++)
	{
		const unsigned long* src =
			(const unsigned long*)((const unsigned char*)rect.pBits + (size_t)y * rect.Pitch);

		for (long x = 0; x < shot.w; x++)
		{
			const unsigned long c = src[x] | 0xFF000000u;
			pixels[y * shot.w + x] = c;

			if ((c & 0x00FFFFFFu) == 0x00FF00FFu)
				background++;

			sum += (double)((c >> 16) & 255) + (double)((c >> 8) & 255) + (double)(c & 255);

			/* The peak and the white share are how a light shaft is measured
			 * and how the player looks at one - a mean says nothing about a
			 * fan whose middle is saturated. */
			{
				const unsigned long r = (c >> 16) & 255;
				const unsigned long g = (c >> 8) & 255;
				const unsigned long b = c & 255;
				const unsigned long m = r > g ? (r > b ? r : b) : (g > b ? g : b);

				if ((double)m > peak)
					peak = (double)m;

				if (m >= 230)
					white++;
			}
		}
	}

	shot.readback->UnlockRect();
	WetStepLine(pixels, shot.w, shot.h);
	WetCutLine(pixels, shot.w, shot.h);

	const double texels = (double)shot.w * (double)shot.h;
	const bool ok = WritePng(path, pixels, shot.w, shot.h);
	free(pixels);

	printf("  %-58s %5.1f%% background, mean %.1f/255 (%.1f%%), peak %.0f "
		"(%.1f%%), white %.2f%%\n",
		path, 100.0 * (double)background / texels, sum / (texels * 3.0),
		100.0 * sum / (texels * 3.0 * 255.0), peak, 100.0 * peak / 255.0,
		100.0 * (double)white / texels);

	if (!ok)
		printf("RoomProbe: writing %s failed\n", path);

	return ok;
}

static void ShotEnd(Shot& shot)
{
	if (shot.oldColour)
	{
		g_device->SetRenderTarget(0, shot.oldColour);
		shot.oldColour->Release();
		shot.oldColour = 0;
	}

	HDRenderWaterDepthUnbind();
	g_device->SetDepthStencilSurface(0);

	if (shot.readback) { shot.readback->Release(); shot.readback = 0; }
	if (shot.depth) { shot.depth->Release(); shot.depth = 0; }
	if (shot.colour) { shot.colour->Release(); shot.colour = 0; }
	if (shot.target) { shot.target->Release(); shot.target = 0; }
}

/* ------------------------------------------------------ scissor proof -- */

/* What was drawn inside the scissor window and what was drawn outside it.
 * The picture is read back and every pixel asked one question: is it still
 * the colour ShotBegin cleared it to? A pixel that is not was drawn.
 *
 * The background is magenta unless -black said otherwise, and that matters
 * here more than anywhere else in this tool: on a black background a black
 * pixel of geometry cannot be told from a pixel nobody touched, and the whole
 * count would read zero whether the scissor worked or not. So -black turns
 * the proof into a warning and no number. */
static void ScissorProof(Shot& shot, const RECT& box)
{
	D3DLOCKED_RECT rect;

	if (g_black)
	{
		printf("group M: -scissor cannot be proved with -black - a black pixel "
			"of geometry and an untouched pixel are the same colour.\n");
		return;
	}

	if (FAILED(g_device->GetRenderTargetData(shot.colour, shot.readback)) ||
		FAILED(shot.readback->LockRect(&rect, 0, D3DLOCK_READONLY)))
	{
		printf("group M: reading the picture back for the scissor proof failed\n");
		return;
	}

	long long inside = 0, outside = 0, insideArea = 0;

	for (long y = 0; y < shot.h; y++)
	{
		const unsigned long* src =
			(const unsigned long*)((const unsigned char*)rect.pBits + (size_t)y * rect.Pitch);

		for (long x = 0; x < shot.w; x++)
		{
			const bool in = x >= box.left && x < box.right &&
				y >= box.top && y < box.bottom;

			if (in)
				insideArea++;

			if ((src[x] & 0x00FFFFFFu) == 0x00FF00FFu)
				continue;		// nobody touched it

			if (in)
				inside++;
			else
				outside++;
		}
	}

	shot.readback->UnlockRect();

	printf("group M: scissor %ld,%ld,%ld,%ld over %ldx%ld - %lld pixel(s) "
		"drawn inside it (%.1f%% of the rectangle), %lld outside - %s\n",
		box.left, box.top, box.right, box.bottom, shot.w, shot.h,
		inside, insideArea ? 100.0 * (double)inside / (double)insideArea : 0.0,
		outside,
		outside ? "FAIL, the scissor test let something through"
			: inside ? "PASS, nothing was drawn outside the window"
			: "INCONCLUSIVE, nothing was drawn at all - move the camera");
}

/* ------------------------------------------------------- matrix proof -- */

/* The engine's transform against the HD matrix, in pixels.
 *
 * The classic path (specific\drawroom.cpp, aRoomletTransformLight) takes a
 * room-local vertex through the matrix on the stack as a row vector and then
 * divides by z itself:
 *
 *     v  = pos * M            M = view * world, translation in row 3
 *     sx = v.x * persp / v.z + centerx
 *     sy = v.y * persp / v.z + centery
 *     sz = f_a - f_boo * (2048 / v.z)
 *
 * The HD path hands the device M * projection and lets the hardware divide by
 * w and apply the viewport. Those three lines are reproduced below exactly as
 * they stand in the engine - not approximated - and compared with the matrix
 * that really goes to the device, over the room's own vertices.
 *
 * The same comparison runs inside the game (specific\hd\hd_level.cpp,
 * MatrixCheck) on the first HD frame of a level and writes one HDLOG line.
 * This is that proof without the game. */
static void MatrixProof(int room, const float* view, const D3DMATRIX& projection,
	double persp, double width, double height, double a, double boo)
{
	short xyz[HD_RENDER_SAMPLES * 3];
	const int count = HDRenderGroupSamples(room, xyz, HD_RENDER_SAMPLES);

	printf("\n-- the world matrix -----------------------------------------\n");

	if (count <= 0)
	{
		printf("group %d has no sample vertices - nothing to prove\n", room);
		return;
	}

	/* world * projection, the general 4x4 as hd_render.cpp builds it and as
	 * the vertex shader uses it: mul(float4(pos, 1), gWorldViewProj). The
	 * probe draws the room with world = identity, so the engine's "view *
	 * world" is this view. */
	const float* p = (const float*)&projection;
	double wvp[16];

	for (int row = 0; row < 4; row++)
		for (int col = 0; col < 4; col++)
		{
			double sum = 0;

			for (int k = 0; k < 4; k++)
				sum += (double)view[row * 4 + k] * (double)p[k * 4 + col];

			wvp[row * 4 + col] = sum;
		}

	const double centerX = width * 0.5;
	const double centerY = height * 0.5;
	const double znear = 20.0;		// f_mznear, the engine's own

	double worstX = 0, worstY = 0, worstZ = 0;
	int used = 0, behind = 0, shown = 0;

	printf("%d sample vertices of group %d, camera as above\n", count, room);
	printf("\n  %-24s %-19s %-19s %s\n",
		"room-local x y z", "classic sx, sy", "device sx, sy", "difference (px)");

	for (int i = 0; i < count; i++)
	{
		const float px = (float)xyz[i * 3 + 0];
		const float py = (float)xyz[i * 3 + 1];
		const float pz = (float)xyz[i * 3 + 2];

		// --- the classic path, line for line ---------------------------
		const float vx = px * view[0] + py * view[4] + pz * view[8] + view[12];
		const float vy = px * view[1] + py * view[5] + pz * view[9] + view[13];
		const float vz = px * view[2] + py * view[6] + pz * view[10] + view[14];

		if (vz < (float)znear)
		{
			behind++;
			continue;
		}

		const float zv = (float)persp / vz;
		const double classicX = (double)(vx * zv + (float)centerX);
		const double classicY = (double)(vy * zv + (float)centerY);
		const double classicZ = a - boo * (2048.0 / (double)vz);

		// --- the device -------------------------------------------------
		const double cx = (double)px * wvp[0] + (double)py * wvp[4] + (double)pz * wvp[8] + wvp[12];
		const double cy = (double)px * wvp[1] + (double)py * wvp[5] + (double)pz * wvp[9] + wvp[13];
		const double cz = (double)px * wvp[2] + (double)py * wvp[6] + (double)pz * wvp[10] + wvp[14];
		const double cw = (double)px * wvp[3] + (double)py * wvp[7] + (double)pz * wvp[11] + wvp[15];

		if (cw <= 0.0)
		{
			behind++;
			continue;
		}

		// The viewport this engine never sets: the whole target, MinZ 0,
		// MaxZ 1.
		const double deviceX = (cx / cw + 1.0) * 0.5 * width;
		const double deviceY = (1.0 - cy / cw) * 0.5 * height;
		const double deviceZ = cz / cw;

		const double dx = fabs(deviceX - classicX);
		const double dy = fabs(deviceY - classicY);
		const double dz = fabs(deviceZ - classicZ);

		if (dx > worstX) worstX = dx;
		if (dy > worstY) worstY = dy;
		if (dz > worstZ) worstZ = dz;

		used++;

		// A dozen rows are a table; a hundred are a wall.
		if (shown < 12 && (i % (count / 12 + 1)) == 0)
		{
			printf("  %7.0f %7.0f %7.0f  %8.2f %8.2f  %8.2f %8.2f  %.5f %.5f\n",
				(double)px, (double)py, (double)pz,
				classicX, classicY, deviceX, deviceY, dx, dy);
			shown++;
		}
	}

	printf("\n%d of %d vertices in front of the near plane (%d behind it or "
		"off the near side)\n", used, count, behind);
	printf("worst difference: x %.6f px, y %.6f px, depth %.9f\n",
		worstX, worstY, worstZ);
	printf("verdict: %s\n", (worstX < 1.0 && worstY < 1.0 && worstZ < 1e-5)
		? "PASS - the HD matrix puts the point where the engine puts it"
		: "FAIL - the two paths disagree, an HD room would not line up");
}

/* ---------------------------------------------- the light and fog state -- */

static HDLightSource g_lights[HD_MAX_LIGHTS];
static int g_light_count = 0;
static HDFogBulb g_bulbs[HD_MAX_BULBS];
static int g_bulb_count = 0;
static double g_fog_start = 0;
static bool g_underwater = false;
static bool g_shimmer = false;
static double g_wibble = 0;
static int g_mesh_effect = 0;

/* -time sets the game clock the UV scroll and the wind ride on; -water turns
 * the water surface wobble on for the whole drawn room (through sel1[3], the
 * same fourth flag byte -shimmer uses); -motion off takes all three motions
 * away again, which is what the engine's hd_motion does. */
static double g_time = 0;
static bool g_water = false;
// -hdwater 0|1 (-1 not asked) and -waterfile - see the usage above.
static int g_hdwater = -1;
static std::string g_water_file;
/* -underlook: the sea seen from below as the game draws it
 * (HDRenderWaterLook), right after the sky; -underlookknobs the same with
 * other numbers in place of HDWaterLookKnobs' (to choose them).
 * -underlookset: the study over the whole frame (HDRenderWaterProbeLook),
 * its surface the body's the extinction measures against, with its nine
 * numbers - the disc, the shafts and the underside's strengths, the
 * surface light's reach, the disc's power, the shafts' cell size, the
 * noise's floor, their reach under the surface and the march's length. */
static bool g_under_look = false;
static bool g_under_game = false;
static bool g_under_game_knobs = false;
static float g_under_knobs[HD_LOOK_KNOBS] = { 3.0f, 3.0f, 0.25f,
	40000.0f, 600.0f, 700.0f, 0.55f, 6000.0f, 8000.0f, 0.0f, 0.0f };
static float g_under_game_set[HD_LOOK_KNOBS] = {};
/* -surfaceroom: the room whose art is the sea's surface, printed;
 * -surfacealways: that room drawn as the game draws it at every frame of a
 * level that takes the deep look - added to the -with list where the
 * picture leaves it out, at its offset from the seed room. */
static bool g_surface_room = false;
static bool g_surface_always = false;

// The switches, out of ProbeMain's chain of else-ifs (C1061).
static bool UnderArg(int argc, char** argv, int* at)
{
	if (!strcmp(argv[*at], "-underlook"))
	{
		g_under_game = true;
		return true;
	}

	if (!strcmp(argv[*at], "-surfaceroom"))
	{
		g_surface_room = true;
		return true;
	}

	if (!strcmp(argv[*at], "-surfacealways"))
	{
		g_surface_always = true;
		return true;
	}

	if (!strcmp(argv[*at], "-underlookknobs") && *at + HD_LOOK_KNOBS < argc)
	{
		for (int k = 0; k < HD_LOOK_KNOBS; k++)
			g_under_game_set[k] = (float)atof(argv[*at + 1 + k]);

		g_under_game = true;
		g_under_game_knobs = true;
		*at += HD_LOOK_KNOBS;
		return true;
	}

	if (!strcmp(argv[*at], "-underlookset") && *at + 9 < argc)
	{
		for (int k = 0; k < 9; k++)
			g_under_knobs[k] = (float)atof(argv[*at + 1 + k]);

		g_under_look = true;
		*at += 9;
		return true;
	}

	return false;
}
// -ripple x,z,age[,size]: the rings handed to every picture.
struct ProbeRipple
{
	float x, z, age, size;
};
static std::vector<ProbeRipple> g_ripples;
// -lara x,z: Lara's place for the waterline's gap round her.
static bool g_lara_set = false;
static float g_lara_x = 0.0f, g_lara_z = 0.0f;
// -foot x1,z1,x2,z2,r and -swirl x,z,r: the feet handed to every picture,
// five numbers each.
static std::vector<float> g_feet;
/* -creature x,z: stand-ins for swimming creatures, as x, z, drawn with the
 * creatures' mark where the HD objects are drawn (HDRenderWaterProbe-
 * Creature); the seed room's classic origin shifts them into the picture. */
static std::vector<float> g_creatures;
// -wader x,z: stand-ins for a wading Lara, as x, z, drawn as -creature's.
static std::vector<float> g_waders;
/* -swimmer x,z,below: stand-ins for a body swimming under the surface, as
 * x, z, below, drawn as -creature's. */
static std::vector<float> g_swimmers;
/* -wetstand x,z[,h]: stand-ins for Lara on a wet floor, as x, z, h (768,
 * her standing height, without h): a dark box on the wet square there,
 * drawn where the HD objects are and through the wet floor's mirror of the
 * models (HDRenderWaterProbeStand, HDRenderWaterWetModels). */
static std::vector<float> g_wet_stands;
// -wetstand's a: the alpha the boxes' pixels write.
static int g_wet_stand_alpha = 255;
// The frame's projection, for the models' mirror (ProbeWetDraw).
static float g_wet_stand_proj[16];
/* -wetnomodels: the stand-ins drawn without the models' mirror - the
 * picture the march alone makes of them, the before picture out of the
 * same build. */
static bool g_wet_no_models = false;

#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_lara.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_bind.h"

/* -wetlara x,y,z[,yaw[,pose]]: Lara herself on a wet floor - the level's
 * outfit built as the engine builds it (HDLaraPlan, HDModelLevelBegin,
 * HDLaraLevelBuild), her parts drawn through HDModelDraw on node matrices
 * of the classic skeleton's bind walk (as ModelProbe's), standing (pose 0)
 * or on all fours (pose 1, the crawl: hips, thighs, shins and upper arms
 * turned a right angle each), her lowest joint 40 units over y (her
 * floor), turned by yaw degrees; drawn among the frame's objects and
 * through the wet floor's mirror of the models in place of -wetstand's
 * boxes. */
static bool g_wet_lara = false;
static float g_wet_lara_at[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static int g_wet_lara_pose = 0;
static bool g_wet_lara_ready = false;
static float g_wet_lara_bind[HD_LARA_NODES][3];
static long g_wet_lara_flags[HD_LARA_NODES];
static float g_wet_lara_mats[HD_LARA_NODES * 16];


static float g_seed_origin[2] = { 0.0f, 0.0f };

/* -life <seed>: the water's life (hd_life_rules.h) on the level's water
 * bodies, built as the game builds it (hd_life.cpp, HDLifeLevelBegin) and
 * rolled with the engine's draw generator started at <seed>, stepped from
 * clock 0 to -time at thirty steps a second - the game's tick - and drawn
 * where HDFrameDraw draws the HD objects. -lifelara x,z[,t]: Lara's place
 * for the school's flight and the drips' rings from clock t on (0 without);
 * without it no school flees and no drip rings. Off by default: every
 * picture without it is the picture it was. */
static bool g_life = false;
static unsigned g_life_seed = 0;
static bool g_life_lara = false;
// -lifebench: the life's draws timed on a second pass (LifeFrame).
static bool g_life_bench = false;
/* -lifeall: every body of the level takes floats, whatever the level's
 * row lets in (HDLifeTakes) - a test of the rules on water the game keeps
 * clean, such as ANDREA2's fountain basins. */
static bool g_life_all = false;
static float g_life_lara_x = 0.0f, g_life_lara_z = 0.0f, g_life_lara_t = 0.0f;
/* -lifesplash x,z,t: a splash on the water at world x, z from clock t for
 * two seconds, about the game's splash's life (62 ticks), as the engine
 * reads the game's live splashes; repeatable. */
static std::vector<float> g_life_splash;
/* The classic rooms' ceilings for the drips' drops: each room's origin,
 * squares and per square its ceiling byte and the room above, -1 none
 * (Tr5Level::SectorInfo), as the engine's ceiling_at reads ROOM_INFO. */
struct LifeRoom
{
	int x, z, sizeX, sizeZ;
	std::vector<int> ceiling, above;
};
static std::vector<LifeRoom> g_life_rooms;
/* -lifetank 0|1|game: a fish tank whole (1, the default) or shot (0: its
 * fish are gone, as the engine has it once the FISHTANK item is set off);
 * game: as the game's level start leaves it - the FISHTANK item read out
 * of the classic level (-roombox), its start state as InitialiseItem sets
 * it (HDLifeTankStart) and the engine's rule (HDLifeTankWhole). */
static bool g_life_tank_whole = true;
static bool g_life_tank_game = false;
// -liferings 0|game: 1 every ring alone (no switch), 0 none, 2 the game's.
static int g_life_rings = 1;
/* -lifeseries <s>: every s seconds of the run a LIFESERIES line a school -
 * its state and each fish's place, depth and distance to the edge of the
 * deep water (HDLifeEdgeDistance) - and once the life's bodies' squares;
 * 0 without. */
static float g_life_series = 0.0f;
// The classic level's FISHTANK items: x, z and flags each.
static std::vector<int> g_life_tank_items;
static HDLifePool g_life_pool;
static const HDLifeLevel* g_life_row = 0;
static float g_life_box[HD_LIFE_KINDS][6];
static std::vector<float> g_life_light;

static int LifeDraw(void* state)
{
	return HDLifeDraw((unsigned*)state);
}

/* The level's pool: its models through the engine's model path, its bodies
 * through the water module's getters, the roll, the steps to -time and the
 * drips' rings that are still running then (a ring of the sheets runs about
 * a second and a half; RoomProbe -ripple draws it at its age). The LIFELOG
 * lines are the engine's. */
/* The ceiling over world x, z from room `at` up, through the room above
 * where the ceiling is an opening - the engine's ceiling_at (hd_life.cpp)
 * on the tool's copy of the classic rooms. */
static bool LifeCeilingAt(int at, int x, int z, int* y, int* in)
{
	for (int guard = 0; guard < 16 && at >= 0 &&
		at < (int)g_life_rooms.size(); guard++)
	{
		const LifeRoom& r = g_life_rooms[(size_t)at];
		const int sx = (x - r.x) >> 10, sz = (z - r.z) >> 10;

		if (sx < 0 || sz < 0 || sx >= r.sizeX || sz >= r.sizeZ ||
			r.ceiling.empty())
			return false;

		const size_t k = (size_t)(sx * r.sizeZ + sz);

		if (r.above[k] < 0)
		{
			*y = r.ceiling[k] * 256;
			*in = at;
			return true;
		}

		at = r.above[k];
	}

	return false;
}

static void LifeBuild(const char* root, const char* stem)
{
	g_life_row = HDLifeLevelOf(stem);

	if (!g_life_row)
	{
		printf("LIFELOG level=%s no life in this level\n", stem);
		return;
	}

	const char* why = "";
	static bool modelUp = false;

	if (!modelUp && !HDModelInit(g_device, &why))
	{
		printf("-life: HDModelInit failed - %s\n", why);
		return;
	}

	modelUp = true;

	std::vector<HDModelSlot> slots;

	for (int k = 0; k < HD_LIFE_KINDS; k++)
	{
		const bool used = k == HD_LIFE_FISH ? g_life_row->fish != 0 :
			g_life_row->weight[k] > 0;

		if (!used)
			continue;

		HDModelSlot one;

		one.slot = 0x7800 + k;
		one.nodes = 1;
		one.name = kHDLifeKinds[k].model;
		one.rigidNode = 0;
		slots.push_back(one);
	}

	HDModelStats stats;

	if (slots.empty() || !HDModelLevelBegin(g_device, root, &slots[0],
		(int)slots.size(), false, 1, &stats, &why))
	{
		printf("-life: HDModelLevelBegin failed - %s\n", why);
		return;
	}

	// The models' cost: what their pages added to the page cache. And the
	// warm-up the engine's level build gives its models (HDModelPreload).
	printf("LIFELOG level=%s models loaded=%d (5:%d 4:%d) pages=%d "
		"page_mib=%.2f verts=%d preloaded=%d\n", stem, stats.loaded,
		stats.fromGame5, stats.fromGame4, stats.pageStats.slots,
		stats.pageStats.bytes / 1048576.0, stats.vertices, HDModelPreload());

	char models[160] = { 0 };

	for (int k = 0; k < HD_LIFE_KINDS; k++)
	{
		float* box = g_life_box[k];

		g_life_pool.has[k] = HDModelHas(0x7800 + k) &&
			HDModelBounds(0x7800 + k, box, box + 3) ? 1 : 0;
		g_life_pool.height[k] = g_life_pool.has[k] ?
			HDLifeHeight(kHDLifeKinds[k].rest, box, box + 3) : 0.0f;
		g_life_pool.reach[k] = g_life_pool.has[k] ?
			HDLifeReach(kHDLifeKinds[k].rest, box, box + 3) : 0.0f;

		if (g_life_pool.has[k])
		{
			strcat_s(models, models[0] ? "," : "");
			strcat_s(models, kHDLifeKinds[k].name);
			printf("-life: %s %s box %.0f %.0f %.0f .. %.0f %.0f %.0f "
				"floating height %.0f\n", kHDLifeKinds[k].name,
				kHDLifeKinds[k].model, box[0], box[1], box[2], box[3], box[4],
				box[5], g_life_pool.height[k]);
		}
	}

	g_life_pool.bodies.clear();
	g_life_light.clear();

	for (int w = 0; w < HDRenderWaterBodies(); w++)
	{
		HDRenderWaterBody info;
		float rise = 0.0f;
		const int n = HDRenderWaterBodySquares(w, 0, 0, 0);

		if (n <= 0 || !HDRenderWaterBodyInfo(w, &info) || info.noSheet)
			continue;

		std::vector<HDRenderWaterSquare> squares((size_t)n);

		HDRenderWaterBodySquares(w, &squares[0], n, &rise);

		HDLifeBody b;

		b.index = w;
		b.surfaceY = info.surfaceY;
		b.flowX = info.flow[0];
		b.flowZ = info.flow[1];
		b.speed = info.speed;
		b.river = info.cls == HD_WATER_RIVER ? 1 : 0;
		b.channel = info.cls == HD_WATER_CHANNEL ? 1 : 0;
		b.rise = rise;
		b.upMin = b.upMax = 0.0f;
		b.spring = info.spring;
		b.springX = info.springAt[0];
		b.springZ = info.springAt[1];

		for (int i = 0; i < n; i++)
			if (squares[(size_t)i].sheet)
			{
				HDLifeSquare s;

				s.x = squares[(size_t)i].x;
				s.z = squares[(size_t)i].z;
				s.depth = squares[(size_t)i].depth;
				s.closed = squares[(size_t)i].open ? 0 : 1;
				s.room = squares[(size_t)i].room;
				b.squares.push_back(s);
			}

		if (!g_life_all && !HDLifeTakes(*g_life_row,
			(int)b.squares.size(), b.river, b.channel))
			continue;

		g_life_pool.bodies.push_back(b);

		for (int k = 0; k < 3; k++)
			g_life_light.push_back(info.light[k] * 255.0f);
	}

	unsigned state = g_life_seed;

	HDLifeBuild(g_life_pool, *g_life_row, LifeDraw, &state);

	// The level's fish tanks, whole or shot as -lifetank says.
	for (size_t i = 0; i < sizeof(kHDLifeTanks) / sizeof(kHDLifeTanks[0]);
		i++)
		if (!_stricmp(kHDLifeTanks[i].stem, stem))
		{
			HDLifeAddTank(g_life_pool, kHDLifeTanks[i], LifeDraw, &state);
			g_life_pool.bodies.back().gone = g_life_tank_whole ? 0 : 1;

			/* The game's state: the FISHTANK item within two squares of the
			 * tank's middle, as hd_life.cpp's tank_item finds it. */
			if (g_life_tank_game)
			{
				const HDLifeTank& t = kHDLifeTanks[i];
				const int mx = (t.x0 + t.x1) / 2, mz = (t.z0 + t.z1) / 2;
				int found = 0, active = 0, status = 0, water = 0, flags = 0;

				for (size_t k = 0; k + 2 < g_life_tank_items.size(); k += 3)
					if (abs(g_life_tank_items[k] - mx) < 2048 &&
						abs(g_life_tank_items[k + 1] - mz) < 2048)
					{
						found = 1;
						flags = g_life_tank_items[k + 2];
						HDLifeTankStart(flags, &active, &status, &water);
						break;
					}

				const bool whole = HDLifeTankWhole(found, active, status,
					water);

				g_life_pool.bodies.back().gone = whole ? 0 : 1;
				printf("LIFELOG level=%s tank=%d game_state found=%d "
					"flags=0x%X active=%d status=%d water=%d whole=%d\n", stem,
					(int)i, found, flags, active, status, water, whole ? 1 : 0);
			}

			for (int k = 0; k < 3; k++)
				g_life_light.push_back(kHDLifeTankLight);
		}

	// The drips' ceilings and their drops' fall, as the engine finds them.
	for (size_t i = 0; i < g_life_pool.drips.size(); i++)
	{
		HDLifeDrip& d = g_life_pool.drips[i];

		if (LifeCeilingAt(d.room, (int)d.x, (int)d.z, &d.ceilY, &d.ceilRoom))
			d.fall = HDLifeDropTicks(d.ceilY + kHDLifeDropUnder,
				g_life_pool.bodies[(size_t)d.body].surfaceY);
	}

	size_t bytes = sizeof(g_life_pool) +
		g_life_pool.entries.size() * sizeof(HDLifeEntry) +
		g_life_pool.schools.size() * sizeof(HDLifeSchool) +
		g_life_pool.drips.size() * sizeof(HDLifeDrip) +
		g_life_pool.bodies.size() * sizeof(HDLifeBody);

	for (size_t i = 0; i < g_life_pool.bodies.size(); i++)
		bytes += g_life_pool.bodies[i].squares.size() * sizeof(HDLifeSquare);

	printf("LIFELOG level=%s bodies=%d floats=%d fish=%d drips=%d "
		"skipped=%d bytes=%d models=%s kept=0\n", stem,
		(int)g_life_pool.bodies.size(), g_life_pool.floats, g_life_pool.fish,
		(int)g_life_pool.drips.size(), g_life_pool.unfit, (int)bytes,
		models[0] ? models : "none");

	for (size_t bi = 0; bi < g_life_pool.bodies.size(); bi++)
	{
		const HDLifeBody& b = g_life_pool.bodies[bi];
		int floats = 0, fish = 0, drips = 0, closed = 0;

		for (size_t i = 0; i < g_life_pool.entries.size(); i++)
			if (g_life_pool.entries[i].body == (int)bi)
				(g_life_pool.entries[i].kind == HD_LIFE_FISH ? fish :
					floats)++;

		for (size_t i = 0; i < g_life_pool.drips.size(); i++)
			drips += g_life_pool.drips[i].body == (int)bi ? 1 : 0;

		for (size_t i = 0; i < b.squares.size(); i++)
			closed += b.squares[i].closed;

		char tail[512];

		HDLifeBodyTail(g_life_pool, (int)bi, tail, sizeof(tail));
		printf("LIFELOG level=%s body=%d water=%d class=%s squares=%d "
			"rise=%.1f flow=%.2f,%.2f,%.0f floats=%d fish=%d drips=%d "
			"deep=%d closed=%d %s\n", stem, (int)bi, b.index,
			b.tank ? "tank" : b.river ? "river" : b.channel ? "channel" :
			"other",
			(int)b.squares.size(), b.rise, b.flowX, b.flowZ, b.speed, floats,
			fish, drips, HDLifeDeepCount(b), closed, tail);
	}

	/* -lifeseries: the bodies' squares, `x,z,depth` each, for the maps the
	 * series' places are read against. */
	for (size_t bi = 0; g_life_series > 0.0f &&
		bi < g_life_pool.bodies.size(); bi++)
	{
		const HDLifeBody& b = g_life_pool.bodies[bi];

		printf("LIFESQUARES level=%s body=%d", stem, (int)bi);

		for (size_t i = 0; i < b.squares.size(); i++)
			printf("%s%d,%d,%d", i ? ";" : " ", b.squares[i].x,
				b.squares[i].z, b.squares[i].depth);

		printf("\n");
	}

	const int every = g_life_series > 0.0f ? (int)(g_life_series * 30.0f +
		0.5f) : 0;

	// The game's steps from clock 0 to -time, and the tick's cost.
	LARGE_INTEGER f, t0, t1;
	std::vector<std::pair<HDLifeRing, float> > rings;
	std::vector<std::pair<HDLifeDrop, float> > drops;
	const int steps = (int)(g_time * 30.0 + 0.5);

	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t0);
	HDLifeTick(g_life_pool, g_life_row, 0.0f, 0, 0.0f, 0.0f, 0);

	for (int s = 1; s <= steps; s++)
	{
		const float t = (float)s / 30.0f;
		const int on = g_life_lara && t >= g_life_lara_t ? 1 : 0;
		std::vector<HDLifeRing> now;
		std::vector<float> wet;

		for (size_t k = 0; k + 2 < g_life_splash.size(); k += 3)
			if (t >= g_life_splash[k + 2] && t < g_life_splash[k + 2] + 2.0f)
			{
				wet.push_back(g_life_splash[k]);
				wet.push_back(g_life_splash[k + 1]);
			}

		std::vector<HDLifeDrop> fell;

		HDLifeTick(g_life_pool, g_life_row, t, on, g_life_lara_x,
			g_life_lara_z, &now, wet.empty() ? 0 : &wet[0],
			(int)wet.size() / 2, &fell);

		for (size_t i = 0; i < fell.size(); i++)
			drops.push_back(std::make_pair(fell[i], t));

		for (size_t i = 0; i < now.size(); i++)
			rings.push_back(std::make_pair(now[i], t));

		/* -lifeseries: a line a school - its centre, flight and darts, and
		 * each fish's entry, place, depth under the surface and distance to
		 * the edge of the water 768 deep (HDLifeEdgeDistance, 2048 at most).
		 * Printed inside the timed steps: a series' tick_ms is not a cost. */
		if (!every || s % every)
			continue;

		for (size_t si = 0; si < g_life_pool.schools.size(); si++)
		{
			const HDLifeSchool& sc = g_life_pool.schools[si];
			const HDLifeBody& sb = g_life_pool.bodies[(size_t)sc.body];

			printf("LIFESERIES level=%s t=%.2f school=%d body=%d lara=%d "
				"centre=%.0f,%.0f darting=%d darts=%d fish=", stem, t,
				(int)si, sc.body, on, sc.cx, sc.cz, sc.darting, sc.darts);

			for (size_t i = 0, n = 0; i < g_life_pool.entries.size(); i++)
			{
				const HDLifeEntry& e = g_life_pool.entries[i];

				if (e.school != (int)si)
					continue;

				float x, y, z, yaw, pitch, roll;

				HDLifePose(g_life_pool, e, t, &x, &y, &z, &yaw, &pitch, &roll);
				printf("%s%d:%.0f,%.0f,%.0f,%.0f", n++ ? ";" : "", (int)i, x,
					z, y - (float)sb.surfaceY, HDLifeEdgeDistance(sb, x, z,
					(float)kHDLifeDeep, 2));
			}

			printf("\n");
		}
	}

	QueryPerformanceCounter(&t1);

	const double ms = steps ? (double)(t1.QuadPart - t0.QuadPart) * 1000.0 /
		(double)f.QuadPart / steps : 0.0;
	int fleeing = 0, shown = 0;

	for (size_t i = 0; i < g_life_pool.schools.size(); i++)
		fleeing += g_life_pool.schools[i].fleeUntil > (float)g_time ? 1 : 0;

	/* -liferings game: the rings as the game's sheets keep them out of the
	 * ripple slots (hd_render.cpp, HDRenderWaterRipples and "the rings"): a
	 * new ring within 192 units of a live one of its kind (the life's are
	 * all a body's, flags 0x10) takes that ring's place when the live one
	 * is 0.6 s old or more and goes into it when younger; a ring lives
	 * 3.5 s. Those three numbers are kRingMerge, kRingRestart and kRingLife
	 * there. -liferings 0 shows none; without the switch every ring of the
	 * last 1.5 s is shown on its own. */
	struct Kept
	{
		float x, z, born, size;
	};
	std::vector<Kept> kept;

	for (size_t i = 0; g_life_rings == 2 && i < rings.size(); i++)
	{
		const Kept k = { rings[i].first.x, rings[i].first.z, rings[i].second,
			(float)rings[i].first.size };
		Kept* into = 0;
		float best = 192.0f * 192.0f;

		for (Kept& g : kept)
		{
			const float d2 = (g.x - k.x) * (g.x - k.x) + (g.z - k.z) *
				(g.z - k.z);

			if (d2 <= best && k.born - g.born <= 3.5f)
			{
				best = d2;
				into = &g;
			}
		}

		if (into && k.born - into->born >= 0.6f)
			*into = k;
		else if (!into)
			kept.push_back(k);
	}

	for (const Kept& k : kept)
	{
		const float age = (float)g_time - k.born;

		if (age > 3.5f)
			continue;

		HDRenderWaterRing(k.x, k.z, age, k.size);
		shown++;
	}

	for (size_t i = 0; g_life_rings == 1 && i < rings.size(); i++)
	{
		const float age = (float)g_time - rings[i].second;

		if (age > 1.5f)
			continue;

		HDRenderWaterRing(rings[i].first.x, rings[i].first.z, age,
			(float)rings[i].first.size);
		shown++;
	}

	/* The ripple slots the rings take: a ring of the life holds its slot
	 * about 37 ticks (UpdateSplashes: init up by 4 a tick to its life of
	 * 48 to 63, then the life down by 3) - the most held at once over the
	 * run (slots_max, of the game's 128); and the most rings of the
	 * sheets' 3.5 seconds within 1,024 units of a ring as it starts
	 * (near_max, of the eight a sheet draw takes). */
	int slotsMax = 0, nearMax = 0;

	for (size_t i = 0; i < rings.size(); i++)
	{
		int slots = 0, close = 0;	// `near` is a macro of windows.h

		for (size_t k = 0; k < rings.size(); k++)
		{
			const float age = rings[i].second - rings[k].second;
			const float dx = rings[i].first.x - rings[k].first.x;
			const float dz = rings[i].first.z - rings[k].first.z;

			slots += age >= 0.0f && age < 37.0f / 30.0f ? 1 : 0;
			close += age >= 0.0f && age < 3.5f && dx * dx + dz * dz <
				1024.0f * 1024.0f ? 1 : 0;
		}

		slotsMax = slots > slotsMax ? slots : slotsMax;
		nearMax = close > nearMax ? close : nearMax;
	}

	printf("LIFELOG level=%s probe clock=%.2f steps=%d tick_ms=%.4f "
		"respawned=%d fleeing=%d rings=%d shown=%d slots_max=%d "
		"near_max=%d\n", stem, g_time, steps, ms, g_life_pool.respawned,
		fleeing, (int)rings.size(), shown, slotsMax, nearMax);

	for (size_t i = 0; i < g_life_pool.entries.size(); i++)
	{
		const HDLifeEntry& e = g_life_pool.entries[i];
		float x, y, z, yaw, pitch, roll;

		HDLifePose(g_life_pool, e, (float)g_time, &x, &y, &z, &yaw, &pitch,
			&roll);
		/* And a float's waterline at this clock: its bottom under the
		 * surface at rest and its top over it (the floating height about
		 * the middle; tilt left out), its bob's height and the most the
		 * body's sheet rises - the float hangs over the drawn water only
		 * if `under` falls short of `rise`. */
		const float h = g_life_pool.height[e.kind];
		const float surface =
			(float)g_life_pool.bodies[(size_t)e.body].surfaceY;

		printf("LIFELOG level=%s entry=%d kind=%s body=%d x=%.0f y=%.1f "
			"z=%.0f yaw=%.0f banked=%d gen=%d under=%.1f over=%.1f amp=%.1f "
			"rise=%.1f dive=%.2f down=%.0f pitch=%.1f\n", stem, (int)i,
			kHDLifeKinds[e.kind].name,
			e.body, x, y, z, yaw * 57.29578f, e.banked, e.gen,
			e.kind == HD_LIFE_FISH ? 0.0f : y + 0.5f * h - surface,
			e.kind == HD_LIFE_FISH ? 0.0f : surface - (y - 0.5f * h), e.amp,
			g_life_pool.bodies[(size_t)e.body].rise,
			e.kind == HD_LIFE_FISH ? 0.0f : HDLifeDiveShare(
			g_life_pool.bodies[(size_t)e.body], e.x, e.z, e.entering),
			y - surface,
			(e.kind == HD_LIFE_FISH ? e.pitch : pitch) * 57.29578f);
	}

	/* A line a school: its centre, whether it is in a flight, the darts it
	 * made, and its fish's mean place and their widest distance from it. */
	for (size_t si = 0; si < g_life_pool.schools.size(); si++)
	{
		const HDLifeSchool& s = g_life_pool.schools[si];
		float mx = 0.0f, mz = 0.0f, spread = 0.0f;
		std::vector<std::pair<float, float> > at;

		for (size_t i = 0; i < g_life_pool.entries.size(); i++)
			if (g_life_pool.entries[i].school == (int)si)
			{
				float x, y, z, yaw, pitch, roll;

				HDLifePose(g_life_pool, g_life_pool.entries[i],
					(float)g_time, &x, &y, &z, &yaw, &pitch, &roll);
				at.push_back(std::make_pair(x, z));
				mx += x;
				mz += z;
			}

		if (!at.empty())
		{
			mx /= (float)at.size();
			mz /= (float)at.size();
		}

		for (size_t k = 0; k < at.size(); k++)
		{
			const float d = sqrtf((at[k].first - mx) * (at[k].first - mx) +
				(at[k].second - mz) * (at[k].second - mz));

			spread = d > spread ? d : spread;
		}

		/* And its depth band (HDLifeFishBand), units under the surface,
		 * and its fish's depths now, the least and the most. */
		float top, bottom, least = 0.0f, most = 0.0f;
		const HDLifeBody& sb = g_life_pool.bodies[(size_t)s.body];

		HDLifeFishBand(g_life_pool, sb, &top, &bottom);

		for (size_t i = 0, n = 0; i < g_life_pool.entries.size(); i++)
			if (g_life_pool.entries[i].school == (int)si)
			{
				float x, y, z, yaw, pitch, roll;

				HDLifePose(g_life_pool, g_life_pool.entries[i],
					(float)g_time, &x, &y, &z, &yaw, &pitch, &roll);
				y -= (float)sb.surfaceY;
				least = !n || y < least ? y : least;
				most = !n || y > most ? y : most;
				n++;
			}

		printf("LIFELOG level=%s school=%d body=%d centre=%.0f,%.0f "
			"darting=%d darts=%d fish_at=%.0f,%.0f spread=%.0f "
			"band=%.0f..%.0f down=%.0f..%.0f\n", stem,
			(int)si, s.body, s.cx, s.cz, s.darting, s.darts, mx, mz,
			spread, top, bottom, least, most);
	}

	for (size_t i = 0; i < g_life_pool.drips.size(); i++)
	{
		const HDLifeDrip& d = g_life_pool.drips[i];

		printf("LIFELOG level=%s drip=%d body=%d x=%.0f z=%.0f period=%.2f "
			"phase=%.2f fired=%d last=%.2f ceil=%d room=%d fall=%d\n", stem,
			(int)i, d.body, d.x, d.z, d.period, d.phase, d.fired, d.last,
			d.ceilY, d.ceilRoom, d.fall);
	}

	/* A line a drop: where it left its ceiling, at what clock, the ticks
	 * it falls, and its height at the probe's clock while it falls, as
	 * UpdateDrips steps it (HDLifeDropFall; y_now -1 before or after);
	 * held 1 while the pool holds it to be drawn (HDLifePool::falling). */
	for (size_t i = 0; i < drops.size(); i++)
	{
		const HDLifeDrop& o = drops[i].first;
		const int ticks = (int)(((float)g_time - drops[i].second) * 30.0f);
		const int y = o.y + (int)HDLifeDropFall((float)ticks, 0);
		int held = 0;

		for (size_t k = 0; k < g_life_pool.falling.size(); k++)
			held |= g_life_pool.falling[k].x == o.x &&
				g_life_pool.falling[k].z == o.z &&
				g_life_pool.falling[k].at == o.at;

		printf("LIFELOG level=%s drop=%d x=%d z=%d from=%d room=%d "
			"ticks=%d at=%.2f y_now=%d held=%d\n", stem, (int)i, o.x, o.z,
			o.y, o.room, o.ticks, drops[i].second, ticks >= 0 && ticks <
			o.ticks ? y : -1, held);
	}

	for (size_t i = 0; i < rings.size(); i++)
		printf("LIFELOG level=%s ring=%d drip=%d x=%.0f z=%.0f size=%d "
			"at=%.2f\n", stem, (int)i, rings[i].first.drip, rings[i].first.x,
			rings[i].first.z, rings[i].first.size, rings[i].second);
}

/* The drawn entries in `order`, nearest first, at most kHDLifeDrawMax;
 * an entry behind the eye is not drawn. */
static void LifeDrawAll(const std::vector<std::pair<float, int> >& order,
	const float* view, float t, int* drawnOut, int* skippedOut)
{
	int drawn = 0, skipped = 0;

	for (size_t k = 0; k < order.size(); k++)
	{
		if (drawn >= kHDLifeDrawMax)
		{
			skipped++;
			continue;
		}

		const HDLifeEntry& e = g_life_pool.entries[(size_t)order[k].second];
		float x, y, z, yaw, pitch, roll;

		HDLifePose(g_life_pool, e, t, &x, &y, &z, &yaw, &pitch, &roll);

		float local[16], world[16], node[16];
		const float* box = g_life_box[e.kind];

		HDLifeMatrix(kHDLifeKinds[e.kind].rest, box, box + 3, yaw, pitch,
			roll, local);
		memcpy(world, local, sizeof(world));
		world[12] += x - g_seed_origin[0];
		world[13] += y;
		world[14] += z - g_seed_origin[1];

		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++)
				node[r * 4 + c] = world[r * 4 + 0] * view[0 * 4 + c] +
					world[r * 4 + 1] * view[1 * 4 + c] +
					world[r * 4 + 2] * view[2 * 4 + c] +
					world[r * 4 + 3] * view[3 * 4 + c];

		// Behind the eye: its point's view depth, as the engine tests it.
		if ((x - g_seed_origin[0]) * view[2] + y * view[6] +
			(z - g_seed_origin[1]) * view[10] + view[14] < 0.0f)
			continue;

		HDModelLighting lighting;

		lighting.lights = 0;
		lighting.lightCount = 0;
		// A float's fade into its slots, as the engine has it.
		const float fade = HDLifeDiveLightShare(HDLifeDiveShare(
			g_life_pool.bodies[(size_t)e.body], e.x, e.z, e.entering));

		lighting.sunScale = 0.75f * fade;
		lighting.specStrength = 0.0f;
		lighting.specPower = 1.0f;

		const float share = HDLifeLightShare(e.kind) * fade;
		const float light[3] = { g_life_light[(size_t)e.body * 3] * share,
			g_life_light[(size_t)e.body * 3 + 1] * share,
			g_life_light[(size_t)e.body * 3 + 2] * share };

		if (HDModelDraw(0x7800 + e.kind, node, 1, light, ~0u, &lighting))
			drawn++;
		else
			skipped++;
	}

	*drawnOut = drawn;
	*skippedOut = skipped;
}

/* The pool drawn as the game draws it: nearest the eye first, at most
 * kHDLifeDrawMax, each model at its pose with its body's light; the frame's
 * world is the seed room's (x and z less its origin). The draws' GPU time
 * goes to the LIFELOG probe line (a timestamp pair, waited for). */
static void LifeFrame(const float* view, const D3DMATRIX* projection,
	int bulbs, float eyeX, float eyeY, float eyeZ)
{
	if (!g_life_row || g_life_pool.entries.empty())
		return;

	LARGE_INTEGER cf, c0, c1;

	QueryPerformanceFrequency(&cf);
	QueryPerformanceCounter(&c0);

	const float t = (float)g_time;
	std::vector<std::pair<float, int> > order;

	for (size_t i = 0; i < g_life_pool.entries.size(); i++)
	{
		const HDLifeEntry& e = g_life_pool.entries[i];
		float x, y, z, yaw, pitch, roll;

		if (!g_life_pool.has[e.kind] ||
			g_life_pool.bodies[(size_t)e.body].gone)
			continue;

		HDLifePose(g_life_pool, e, t, &x, &y, &z, &yaw, &pitch, &roll);

		const float dx = x - g_seed_origin[0] - eyeX, dy = y - eyeY;
		const float dz = z - g_seed_origin[1] - eyeZ;

		order.push_back(std::make_pair(dx * dx + dy * dy + dz * dz, (int)i));
	}

	std::sort(order.begin(), order.end());

	/* The processor's side as the engine's frame has it: the poses, the
	 * order above and the matrices of the drawn entries, timed apart from
	 * the draws (whose first one pays the device's own uploads here). */
	for (size_t k = 0; k < order.size() && (int)k < kHDLifeDrawMax; k++)
	{
		const HDLifeEntry& e = g_life_pool.entries[(size_t)order[k].second];
		float x, y, z, yaw, pitch, roll, local[16];
		const float* box = g_life_box[e.kind];

		HDLifePose(g_life_pool, e, t, &x, &y, &z, &yaw, &pitch, &roll);
		HDLifeMatrix(kHDLifeKinds[e.kind].rest, box, box + 3, yaw, pitch,
			roll, local);
	}

	QueryPerformanceCounter(&c1);

	const double cpuMs = (double)(c1.QuadPart - c0.QuadPart) * 1000.0 /
		(double)cf.QuadPart;

	/* -lifebench: one untimed pass first, so the timed one pays no first
	 * use of a model or its pages (the engine's frames after the first
	 * do not); it draws every model twice, so it is for the numbers and
	 * not for a picture. */
	if (g_life_bench)
	{
		int d = 0, k = 0;

		HDModelFrameBegin(projection, bulbs, HD_PASS_ALL);
		HDModelClip(0);
		LifeDrawAll(order, view, t, &d, &k);
	}

	IDirect3DQuery9* q[4] = { 0, 0, 0, 0 };

	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &q[0]);
	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1]);
	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2]);
	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &q[3]);

	if (q[0] && q[1] && q[2] && q[3])
	{
		q[0]->Issue(D3DISSUE_BEGIN);
		q[1]->Issue(D3DISSUE_END);
	}

	// Inside the creatures' mark, as HDFrameDraw draws the objects' list.
	HDRenderWaterMarkBegin();
	HDModelFrameBegin(projection, bulbs, HD_PASS_ALL);
	HDModelClip(0);

	int drawn = 0, skipped = 0;

	// The draws' own submission on the processor (draw_ms).
	LARGE_INTEGER d0, d1;

	QueryPerformanceCounter(&d0);
	LifeDrawAll(order, view, t, &drawn, &skipped);
	QueryPerformanceCounter(&d1);

	HDRenderWaterMarkEnd(drawn);

	double gpu = -1.0;

	if (q[0] && q[1] && q[2] && q[3])
	{
		q[2]->Issue(D3DISSUE_END);
		q[3]->Issue(D3DISSUE_END);
		q[0]->Issue(D3DISSUE_END);

		BOOL disjoint = TRUE;
		UINT64 a = 0, b = 0, freq = 0;

		while (q[0]->GetData(&disjoint, sizeof(disjoint), D3DGETDATA_FLUSH) ==
			S_FALSE) {}
		while (q[1]->GetData(&a, sizeof(a), D3DGETDATA_FLUSH) == S_FALSE) {}
		while (q[2]->GetData(&b, sizeof(b), D3DGETDATA_FLUSH) == S_FALSE) {}
		while (q[3]->GetData(&freq, sizeof(freq), D3DGETDATA_FLUSH) ==
			S_FALSE) {}

		if (!disjoint && freq)
			gpu = (double)(b - a) * 1000.0 / (double)freq;
	}

	for (int i = 0; i < 4; i++)
		if (q[i])
			q[i]->Release();

	printf("LIFELOG probe drawn=%d skipped=%d gpu_ms=%.4f cpu_ms=%.4f "
		"draw_ms=%.4f calls=%d\n", drawn, skipped, gpu, cpuMs,
		(double)(d1.QuadPart - d0.QuadPart) * 1000.0 / (double)cf.QuadPart,
		HDModelDrawCalls());
}

/* -splash x y z t, -splashkind and -splashfall: one splash for the cards. */
static bool g_splash_set = false;
static float g_splash_at[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static int g_splash_kind = 0;
static int g_splash_fall = 128;
/* -tank item t: the tank, its film and where its spray comes from - the
 * item's place and turn and its mesh's root (its first frame's offset),
 * which GetJointAbsPosition adds for joint 0. */
static int g_tank_item = -1;
static float g_tank_time = 0.0f;
// -tankleave: 1 the spray, 2 the tank's water left out of the picture.
static int g_tank_leave = 0;
/* -tankwet s: the shot tank's wet floor held at s everywhere it reaches
 * (HDRenderWaterWetFixed), under 0 the water's own way at the tank's age. */
static float g_tank_wet = -1.0f;
/* -tankwetmap: every wet floor's squares as text, room by room, after its
 * SPLASHLOG line (HDRenderWaterWetSquare). -tankwetall lays the floors
 * without their rooms and radius (HDRenderWaterWetLimited). */
static bool g_tank_wet_map = false;
static int g_tank_film = -1;
static float g_tank_root[3] = { 0.0f, 0.0f, 0.0f };
static Tr5Item g_tank_place;
static bool g_tank_found = false;
// -nomark: the stand-ins without the mark (the picture before it);
// -markview: the marked pixels in red over the finished picture.
static bool g_creature_mark = true;
// -twophase: the solid and the blend phase in two brackets, as the game.
static bool g_two_phase = false;
// -latemark: the stand-ins drawn unmarked and marked after the bracket.
static bool g_creature_late = false;
// -rippleat x,y,z: HDRenderWaterSheetUnder's answer, three ints a point.
static std::vector<int> g_ripple_at;
// The WATERLOG mist= lines of ProbeWaterFalls.
static std::vector<std::string> g_mist_lines;
static bool g_mark_view = false;
static bool g_motion = true;
/* -plane: four surface marks at the corners of a box, as the classic path
 * would leave them on the corners of one water quad. The renderer's flood
 * then stamps every HD vertex of the group inside that box at that height. */
static HDWaterMark g_plane_marks[4];
static bool g_plane = false;
/* -watermarks: the classic water marks of every room out of -roombox's
 * .TRC (probemarks, before ProbeMain), with -plane's four after them. */
static bool g_watermarks = false;
/* The gloss: tomb5.hd_reflect, on by default here as it is in the game, off
 * with -noreflect so that the before picture can be drawn out of the same
 * build. */
static bool g_reflect = true;
/* -glossmax: every draw call that can take the sheen takes it at strength 1
 * and F0 1 (HDRenderGlossOverride) - the sheen's upper bound. -glossfile: an
 * hd_gloss.txt to build with (HDRenderGlossFile), none without it, where the
 * game reads the one beside its executable. */
static bool g_gloss_max = false;
static std::string g_gloss_file;
/* -glossfield: the light field drawn in place of the surfaces that can take
 * the sheen (HDRenderGlossFieldView), 1 its colour and 2 its direction; 0
 * draws the surfaces. */
static int g_gloss_field = 0;
/* -fieldsight: the light field built with the sight test by the rooms
 * (HDRenderGlossFieldSight), for the comparison with the engine's field,
 * which has none. Needs -roombox. */
static bool g_field_sight = false;
/* Hide every destructible piece of the drawn room, which is what the engine
 * does once Lara has shot the static that stands there - the after picture
 * of a shattered room, out of the same build. */
static bool g_shatter = false;
/* -smash <room>:<mesh> (repeatable): the statics Lara has shot, their pieces
 * hidden as the engine pairs them (probestatics::SmashHide). -smashcensus:
 * the pieces, the shatterable statics and their pairs, then no picture. */
static std::vector<std::pair<int, int> > g_smash;
static bool g_smash_census = false;
/* -smashpiece <n> (repeatable): pieces hidden by number, in any group. */
static std::vector<int> g_smash_pieces;

/* The two switches above, out of the main chain, which stands at the
 * compiler's limit of nested blocks (C1061). */
static bool SmashArg(int argc, char** argv, int* at)
{
	if (!strcmp(argv[*at], "-smashcensus"))
	{
		g_smash_census = true;
		return true;
	}

	if (!strcmp(argv[*at], "-smashpiece") && *at + 1 < argc)
	{
		g_smash_pieces.push_back(atoi(argv[++(*at)]));
		return true;
	}

	if (strcmp(argv[*at], "-smash") || *at + 1 >= argc)
		return false;

	int r = -1, m = -1;

	if (sscanf(argv[++(*at)], "%d:%d", &r, &m) == 2)
		g_smash.push_back(std::make_pair(r, m));

	return true;
}
/* -hasgeom: ask the renderer whether the drawn group has any geometry inside
 * a room-local box - the question hd_level.cpp asks once per static mesh of
 * an HD room. Up to eight boxes per run. */
static float g_geom_box[8][6];
static int g_geom_count = 0;

/* -flatart: ask the renderer whether the drawn group draws anything in one
 * horizontal plane - the question hd_level.cpp asks once per classic water
 * face of an HD room, and the gate that decides whether that face is drawn
 * on top of the HD room or left to the remaster's own art.
 * y, x0, z0, x1, z1, room local. Up to eight per run. */
static int g_flat_box[8][5];
static int g_flat_count = 0;
// -watersurvey: the folder holding the classic .TRC files.
/* The portal window, as a scissor rectangle. -scissor l,t,r,b draws the room
 * with exactly the call the engine makes per room - HDRenderScissor - and
 * then counts, on the picture itself, how many pixels were drawn inside the
 * rectangle and how many outside it. Outside must be zero: that is the whole
 * claim of the portal window, and it is the one part of it a tool without a
 * portal walk can prove. */
static bool g_scissor = false;
static long g_scissor_box[4] = { 0, 0, 0, 0 };

static std::string g_survey;
/* -roombox <folder holding the classic .TRC>: hand the renderer the rooms'
 * own boxes and the list of rooms that may be holding another room's art,
 * exactly as hd_level.cpp's BuildRoomBoxes and BuildRoomIntrusions do, so
 * that the split the engine draws with is the split this tool draws with.
 * Without it every group is one piece and HD_WINDOW_OPEN and
 * HD_WINDOW_INTRUDING both draw the whole room - which is also the proof
 * that a level built with no list is unchanged. */
static std::string g_roombox;
static std::vector<HDRoomBox> g_boxes_built;
static std::vector<HDRoomGrid> g_grids_built;
static std::vector<HDSectorFloor> g_sectors_built;
static std::vector<HDIntrusion> g_intrusions_built;
/* And every room's air (HDRoomAir), as hd_level.cpp's BuildRoomAir hands it
 * over, the squares out of the same floor data words; `g_air_ms` is the
 * time that took, the renderer's own asking added after the build. */
static std::vector<HDRoomAir> g_air_built;
static std::vector<float> g_air_sectors_built;
static std::vector<unsigned char> g_air_links_built;
static double g_air_ms = 0.0;
/* -unlinked 0|1|2: 1, the engine's answer, draws the pieces whose art stands
 * in the air of a room its own has no link with only inside its own
 * window, unless a claimed face of the pair survives; 0 gives them the
 * answer they would have without that, in the same build - the picture
 * to hold the engine's against. 2 is the engine's answer but for a room
 * whose window is empty, whose pieces keep the smaller box's answer: the
 * other answer for such a room, to hold against the engine's. */
static int g_unlinked = 1;
/* -air 0|1|2: the engine's tomb5.hd_air. 1, the default, hands the rooms' air
 * to the build; 0 hands none, as BuildRoomAir does with hd_air off, so no
 * triangle stands in unlinked air and the build is the box's alone; 2 is 1
 * with the hulls (MarkAirHulls in hd_render.cpp). */
static int g_air_option = 1;
/* -vis 1: the engine's tomb5.hd_vis 1 - the walk of BuildRoomVis' path and
 * no carried rooms; the intrusion list and the air are built as with 0. */
static bool g_vis_option = false;
/* -viswin <rule>: how -vis 1 answers the pieces of a group's intruding art
 * (DecideGroup): 0 every piece whole; 1 a piece in unlinked air inside the
 * group's window unless a claimed face of its pair survives the window, the
 * rest whole; 2 every piece as the room-art rules answer it (-decide
 * frame, -unlinked 1), the engine's answer and the default; 3 every piece
 * inside the window; 4 every piece left out; 5 the group's whole art inside
 * the window. */
static int g_vis_win = 2;
// -visfaces 1: each piece's claimed face, its corners, under its decide line.
static bool g_vis_faces = false;
/* -visplane 1, the engine's: on the visibility path a piece in unlinked air
 * stays whole only on a claimed face that survives the window and is one
 * wall with its own face (HDVisWallDrawn, DecidePairs); 0 keeps the pair's
 * answer. */
static bool g_vis_plane = true;
// -gpu <n>: how many timed draws of the frame's solid passes (VisArg).
static int g_gpu_runs = 0;
static int g_window_mode = HD_WINDOW_ALL;

/* -hdm: apply the .hdm sidecar beside the classic .TRC, the way
 * hd_level.cpp's BuildRoomCuts does.
 *
 * This tool has no hd_level.cpp in it - it calls HDRenderLevelBegin itself -
 * so the cut boxes reach the renderer only if something puts them there, and
 * a picture of an "HD room with the doorway cut open" drawn without them
 * would be a picture of the room with the doorway still shut. The sidecar is
 * read with the engine's own reader (hd_markers.cpp), so what the picture
 * shows is what the game will draw.
 *
 * It is a switch and not automatic on purpose: the three pictures that are
 * needed to compare are the classic one, the HD one without the cut and the
 * HD one with it, and the middle one is a run over the very same files with
 * the switch left off. */
static bool g_hdm = false;
static std::vector<HDCut> g_cuts_built;

/* -classicroom <room>: the classic room's own faces in the picture beside the
 * art - the roomlets' quads and triangles out of the .TRC in -roombox
 * (Tr5Level::roomFaces, the faces InsertRoomlet draws), flat grey shaded by
 * their normal, untextured, in the same depth buffer and through the same
 * view and projection as the art. A single-sided face is culled the way the
 * renderer culls the art's opaque ranges, which faces the same way
 * (TrgProbe -classicart measures that agreement). Repeat for more rooms.
 *
 * -classiconly clears the art out of the picture before the classic faces are
 * drawn: the room as the classic path would place it, alone.
 *
 * -coincide <units> counts the pixels at which a classic face lands within
 * `units` of the art in front of the eye - the place a frame that draws both
 * fights over. Two stencil passes of the classic faces against the art's
 * depth, the faces moved `units` towards the eye and then `units` away from
 * it along the line of sight: a pixel the first passes and the second fails
 * is one. Counted with an occlusion query, and of those the ones a third pass
 * of the faces where they are loses against the art's depth - the art's,
 * which a frame that draws both shows instead of the face; -coincidemark
 * paints the first yellow and the second red over the finished picture. */
static std::vector<int> g_classic_rooms;
static bool g_classic_only = false;
/* -classicmark <room>: that room's classic faces red instead of grey, so a
 * picture of the classic faces says which room each pixel belongs to. */
static std::set<int> g_classic_mark;
// -classicplane <y>: only the faces with every corner at that height;
// -classicwet: of those, not the dry copies BuildWaterSurfaces leaves out.
static long g_classic_plane = 0;
static bool g_classic_plane_set = false;
static bool g_classic_wet = false;
static double g_coincide = 0.0;
static bool g_coincide_mark = false;

/* -stand and -skipgroup - see the usage at the top. The lists are the ones
 * hd_level.cpp's BuildStandFaces hands the renderer, built from the .TRC in
 * -roombox: every room's origin and twin, every room with a classic face
 * (less the rooms -hdm names whole) with its whole box grown by
 * HD_COVER_REACH, and the faces in world units. `g_stand_covered` caches each
 * holder's verdict; `g_stand_draws` are this picture's in-holder draws. */
static bool g_stand_rule = false;
// StandState's lines held back while -statictable asks every room.
static bool g_stand_quiet = false;
static std::set<int> g_skip_groups;
static std::vector<HDStandRoom> g_stand_rooms_built;
static std::vector<HDStandHolder> g_stand_holders_built;
static std::vector<HDCoverFace> g_stand_faces_built;
static std::vector<int> g_stand_covered_room;
static std::vector<int> g_stand_covered_answer;
static std::vector<int> g_stand_carrier;
static std::vector<const unsigned char*> g_stand_flags;

struct StandDrawProbe
{
	int group;
	int index;
	bool behind;
	D3DMATRIX world;
};

static std::vector<StandDrawProbe> g_stand_draws;

/* -clipgroup <group> <l,t,r,b>: one group drawn inside a scissor rectangle
 * and every other group drawn whole.
 *
 * `-scissor` puts the rectangle round the whole scene, which is not what the
 * engine ever does: the portal window is a property of one room. To show
 * that in a picture the tool has to be able to cut one group and leave its
 * neighbour alone - which is what this does, with the engine's own
 * HDRenderScissor round that group's own draw calls. */
static int g_clip_group = -1;
static long g_clip_box[4] = { 0, 0, 0, 0 };

/* -tround <group> <l,t,r,b>: draw one group split into two halves - its
 * inside half inside the rectangle the portal walk gave it, its overhang
 * half over the whole screen - and every other group whole. It is the
 * engine's own code path: HDRenderSplitOverhang selects that index layout
 * for the build, and the two calls are the two DrawRoomWindowed made. */
static int g_tround_group = -1;
static long g_tround_box[4] = { 0, 0, 0, 0 };

/* -walk: the classic portal walk (game\draw.cpp, GetRoomBounds and
 * SetRoomBounds), run offline from the classic .TRC alone, with no device,
 * no .TRG and no picture - the two functions copied line for line, with
 * their own names on the numbers.
 *
 * Given an eye and a seed room it prints the room list, and for a room named
 * with -walkwant the reason that room is or is not in it:
 *
 *   reached      a portal from a listed room put it in the list
 *   side         `if (x && (x ^ p->normal[0]) < 0)` - the eye is on the far
 *                side of the portal's own plane
 *   toonear      all four corners fell behind the eye, `tooNear == 4`
 *   empty        the rectangle came out with no width or no height after the
 *                clamp against the room it was seen through
 *   covered      the room's window already covers what this portal would give
 *                it - the early out at the top of SetRoomBounds; not a
 *                refusal, the room is in the list already
 *   not reached  no portal to it out of any listed room, on any test
 *
 * The answer is asked at every yaw of a fan, which also says the thing worth
 * knowing: whether a room is refused at a hair's breadth of one view
 * direction (a portal sliver that crosses the sub pixel threshold, which is
 * ordinary) or over a whole arc (which would be a rule). */
namespace walkoff
{
	struct Room
	{
		long x, y, z;
		long ceiling, floor;		// world y, the classic room box
		int xs, zs;
		unsigned flags;
		short left, right, top, bottom;
		short test_left, test_right, test_top, test_bottom;
		/* -reach: the second rectangle. `reach_*` is to the reach what
		 * `left..bottom` is to the classic window - the union of every
		 * rectangle the room was dequeued with - and `test_reach_*` is to it
		 * what `test_left..test_bottom` is: the one this dequeue carries.
		 * `classic_ok` says whether this dequeue's classic rectangle exists
		 * at all, because with the reach a room can be in the list with no
		 * classic window of any kind. */
		short reach_left, reach_right, reach_top, reach_bottom;
		short test_reach_left, test_reach_right;
		short test_reach_top, test_reach_bottom;
		/* What the engine keeps in g_reach_box and ROOMLIST prints as
		 * reach=: the rectangle of the portal that brought the room in
		 * first, widened by every later one that brings a classic window
		 * with it (game\draw.cpp, SetRoomBoundsReach). */
		short box[4];
		bool classic_ok;
		// True until the reach has visited this room once this frame.
		bool reach_first;
		char bound_active;
		std::vector<Tr5Level::PortalRecord> doors;
		int alt;			// the flipmap twin, -1 for none
	};

	static std::vector<Room> g_room;

	/* The portals the level's closed doors can shut. InitialiseDoor
	 * (game\init.cpp) hands every CLOSED_DOOR1..LIFT_DOORS2 item the portal
	 * of its own room and the one of the room behind it (SetupClosedDoorStuff,
	 * SCDS), and a shut door writes zero into both portals' normals
	 * (ShutThatDoor, game\door.cpp): GetRoomBounds then follows neither. No
	 * log line says which doors are shut, so -walkdump asks which of them
	 * the engine's list says are. Numbered by the rooms of the file, before
	 * any flip - a flip moves a room's portals, zeroed ones included. */
	struct DoorPortals
	{
		int item;
		std::vector<std::pair<int, int> > portals;	// (room, portal index)
	};

	static std::vector<DoorPortals> g_doors;
	// CLOSED_DOOR1 and LIFT_DOORS2 in game\objects.h's slot list.
	static const int kClosedDoor1 = 300;
	static const int kLiftDoors2 = 314;
	/* -walkshut: the door items -walk walks with shut, as -walkdump
	 * prints them (doors_shut=); -1 alone shuts every one of them. */
	static std::vector<int> g_shut_items;
	/* -walkdoors: one line per closed door at the load - its item, slot,
	 * room and place, the room behind it and the corners of every portal
	 * it shuts - what a per-run patch of a level's portals needs. */
	static bool g_door_print = false;
	/* -visportals <room>: that room's portal records at the load, the room
	 * behind, the normal and the world corners; -1 every room's. -2 none. */
	static int g_portal_room = -2;
	// -flip: the rooms whose flipmap is swapped, as FlipMap names them.
	static std::vector<int> g_flip_rooms;
	/* Per room the group that draws it (HDFlipRoom), and the room of its
	 * flipmap pair the portals lead to, -1 for none: what HDCarriedRoom
	 * (hd_level.cpp) asks. Set by Load, swapped by -flip. */
	static std::vector<int> g_group_of;
	static std::vector<int> g_main_of;
	/* The engine's ring is draw_room_list[kRoomListRing] (512) and its list
	 * is draw_rooms[kDrawRoomsMax] (256), see the note over both in
	 * game\draw.cpp. The tool's is deliberately wider than either, because
	 * its job is to say how often the engine's own two would have been
	 * passed - see g_queue_peak and the survey's over60/over100 counters. */
	static const long kRing = 4096;
	static long g_list[kRing];
	static long g_start, g_end;
	static std::vector<short> g_drawn;
	static short g_winxmax, g_winymax;
	static float g_persp, g_centerx, g_centery;
	static float g_view[9];			// the eye's rotation, rows first
	static long g_eye[3];

	// -reach: the two-rectangle walk. Off, this is the plain classic walk.
	static bool g_reach = false;
	/* HDRoomCarries (hd_level.h) as the .TRG and the .TRC answer it: per
	 * room under the sky, the rooms whose art stands in its inside - see
	 * CarryArt in game\draw.cpp. Built by LoadHd, empty with -air 0;
	 * g_carried counts the rooms the last walk took in that way. */
	static std::vector<std::vector<short> > g_carry;
	static int g_carried = 0;
	/* The walk's own brake. ANDREA3 and ANDY2 ran the tool out of memory:
	 * with the parent gone from the child's rectangle, a ring of rooms whose
	 * portals all face the eye can be walked round and round, each lap
	 * widening one room's classic window by a pixel or two and so earning
	 * another queue entry. It terminates - both unions are bounded by the
	 * screen - but "terminates" can mean two million dequeues, and the
	 * verdict list is what runs out of memory first. So: a cap on dequeues
	 * per frame, counted, and the survey turns the verdict list off. */
	static const long kWalkMax = 20000;
	static long g_dequeues = 0;
	static bool g_capped = false;
	static bool g_notes = true;
	// How many rooms of the last walk had NO classic window - the ones only
	// the reach brought in - and the deepest the queue ever got.
	static int g_reach_only = 0;
	static long g_queue_peak = 0;

	/* The camera. The view is always the engine's (enginecam): with
	 * g_use_target set it is built from the eye and g_target as phd_LookAt
	 * builds it, otherwise from the yaw and pitch of the fan, rounded once
	 * to the engine's angle units. g_fov_units, when non-zero, is the field
	 * of view in those units as an ALTERFOV line prints it; zero takes it
	 * from -fov the way InitWindow does. */
	static bool g_use_target = false;
	static long g_target[3] = { 0, 0, 0 };
	static short g_fov_units = 0;
	static short g_xrot = 0, g_yrot = 0;
	/* The roll, zero everywhere but in -walkdump's search for the one a
	 * flyby's spline gave its camera (spotcam.cpp), which no log line
	 * carries. */
	static short g_zrot = 0;
	static long g_persp_units = 0;

	/* Whether each room is drawn by an HD group, the answer HDRoomIsHD
	 * gives: ReachTrimRooms (game\draw.cpp) takes a room the reach alone
	 * brought in back out of the list when it is not. Empty, no room is
	 * taken out for want of art. g_trim_noart and g_trim_budget say how many
	 * rooms the last Trim took out and why. */
	static std::vector<unsigned char> g_hd;
	static int g_trim_noart = 0, g_trim_budget = 0;
	// ReachTrimRooms' budget, kReachBudget.
	static const int kReachBudget = 128;

	// Every portal the walk looked at this run, with what became of it.
	struct Verdict
	{
		int from, to;
		char why[16];
		double n[4];
	};

	static std::vector<Verdict> g_verdict;

	static void Note(int from, int to, const char* why,
		double a, double b, double c, double d)
	{
		Verdict v;

		if (!g_notes)
			return;

		v.from = from;
		v.to = to;
		strncpy(v.why, why, sizeof(v.why) - 1);
		v.why[sizeof(v.why) - 1] = 0;
		v.n[0] = a;
		v.n[1] = b;
		v.n[2] = c;
		v.n[3] = d;
		g_verdict.push_back(v);
	}

	/* SetRoomBounds (game\draw.cpp), with `aMXPtr` handed in as the nine
	 * rotation terms plus the three translation ones that phd_TranslateAbs
	 * works out for the room the portal is seen FROM. */
	static void SetRoomBounds(const Tr5Level::PortalRecord& p, int rn,
		Room& actualRoom, const float* mx, int fromRoom)
	{
		Room& r = g_room[(size_t)rn];
		float vx[4], vy[4], vz[4];
		float x, y, z, tooNear, tL, tR, tT, tB;

		if (r.left <= actualRoom.test_left && r.right >= actualRoom.test_right &&
			r.top <= actualRoom.test_top && r.bottom >= actualRoom.test_bottom)
		{
			Note(fromRoom, rn, "covered", r.left, r.top, r.right, r.bottom);
			return;
		}

		tL = (float)actualRoom.test_right;
		tR = (float)actualRoom.test_left;
		tB = (float)actualRoom.test_top;
		tT = (float)actualRoom.test_bottom;
		tooNear = 0;

		for (int i = 0; i < 4; i++)
		{
			const float dx = (float)p.v[i][0];
			const float dy = (float)p.v[i][1];
			const float dz = (float)p.v[i][2];

			vx[i] = mx[0] * dx + mx[1] * dy + mx[2] * dz + mx[3];
			vy[i] = mx[4] * dx + mx[5] * dy + mx[6] * dz + mx[7];
			vz[i] = mx[8] * dx + mx[9] * dy + mx[10] * dz + mx[11];
			x = vx[i];
			y = vy[i];
			z = vz[i];

			if (z <= 0)
				tooNear++;
			else
			{
				z /= g_persp;

				if (z)
				{
					x = x / z + g_centerx;
					y = y / z + g_centery;
				}
				else
				{
					x = x < 0 ? 0.0f : (float)g_winxmax;
					y = y < 0 ? 0.0f : (float)g_winymax;
				}

				if (x - 1 < tL) tL = x - 1;
				if (x + 1 > tR) tR = x + 1;
				if (y - 1 < tT) tT = y - 1;
				if (y + 1 > tB) tB = y + 1;
			}
		}

		if (tooNear == 4)
		{
			Note(fromRoom, rn, "toonear", 4, 0, 0, 0);
			return;
		}

		if (tooNear > 0)
		{
			for (int i = 0, last = 3; i < 4; last = i, i++)
			{
				if ((vz[last] <= 0) == (vz[i] <= 0))
					continue;

				if (vx[i] < 0 && vx[last] < 0)
					tL = 0;
				else if (vx[i] > 0 && vx[last] > 0)
					tR = (float)g_winxmax;
				else
				{
					tL = 0;
					tR = (float)g_winxmax;
				}

				if (vy[i] < 0 && vy[last] < 0)
					tT = 0;
				else if (vy[i] > 0 && vy[last] > 0)
					tB = (float)g_winymax;
				else
				{
					tT = 0;
					tB = (float)g_winymax;
				}
			}
		}

		if (tL < actualRoom.test_left) tL = actualRoom.test_left;
		if (tR > actualRoom.test_right) tR = actualRoom.test_right;
		if (tT < actualRoom.test_top) tT = actualRoom.test_top;
		if (tB > actualRoom.test_bottom) tB = actualRoom.test_bottom;

		if (tL >= tR || tT >= tB)
		{
			Note(fromRoom, rn, "empty", tL, tT, tR, tB);
			return;
		}

		if (r.bound_active & 2)
		{
			if (tL < r.test_left) r.test_left = (short)tL;
			if (tT < r.test_top) r.test_top = (short)tT;
			if (tR > r.test_right) r.test_right = (short)tR;
			if (tB > r.test_bottom) r.test_bottom = (short)tB;
			Note(fromRoom, rn, "widened", tL, tT, tR, tB);
		}
		else
		{
			g_list[g_end % kRing] = rn;
			g_end++;

			if (g_end - g_start > g_queue_peak)
				g_queue_peak = g_end - g_start;

			r.bound_active |= 2;
			r.test_left = (short)tL;
			r.test_right = (short)tR;
			r.test_top = (short)tT;
			r.test_bottom = (short)tB;
			Note(fromRoom, rn, "reached", tL, tT, tR, tB);
		}
	}

	/* SetRoomBounds with the reach (-reach).
	 *
	 * The same arithmetic on the same four corners, carried into two answers
	 * instead of one:
	 *
	 *   the reach says whether the room is drawn, and it is the portal's own
	 *   projection clamped against the screen - the parent's window does not
	 *   enter it at all. That is what makes a chain run: as long as each
	 *   portal faces the eye and lands on the screen, the room behind it is
	 *   drawn. A room needs this answer once, so it is a plain breadth-first
	 *   visit with a seen-mark.
	 *
	 *   the classic window is what it always was, the projection clamped
	 *   against the parent's classic window, unioned over every portal the
	 *   room is seen through, and it is now allowed not to exist at all. A
	 *   room the reach alone brought in has no classic window: it is drawn,
	 *   and its intruding triangles are drawn inside an empty scissor, which
	 *   is not at all - the bleed guard, kept exactly.
	 *
	 * Clamping the child against the parent's reach instead of a third
	 * reading is the same as the classic walk: with the child clamped
	 * against the parent's reach, one eye's fan of 504 directions in JOBY2
	 * came out at classic mean 1.8 / max 3 and reach mean 1.8 / max 3 - gain
	 * 0 in every single direction, and room 99 refused `empty` on the same
	 * 140 of them. The reason is that the root's reach is the screen, so
	 * reach(child) = projection n reach(parent) unrolls to projection n
	 * projection(parent) n ... n screen, which is the chain of intersections
	 * the classic window already is. Only dropping the parent out of the
	 * child's rectangle changes anything.
	 *
	 * The reach is a seen-mark and not a second rectangle to widen because a
	 * second union per room, re-queuing a room whenever a portal gives it
	 * anything that union does not already hold, runs ANDREA3 and ANDY2 out
	 * of memory: with the parent gone from the child's rectangle a ring of
	 * rooms whose portals all face the eye can be walked round and round,
	 * each lap widening one room's classic window by a pixel and earning
	 * another queue entry - ANDREA3's worst frame took over 20,000 dequeues
	 * against the classic walk's 33. The reach rectangle of a portal does
	 * not depend on the parent - it is the same rectangle however the walk
	 * arrived - so there is nothing to widen and one visit is the whole of
	 * it. With the mark, the dequeues per frame are the classic walk's own
	 * plus at most one per room.
	 *
	 * Everything else is untouched - the side test in the caller, tooNear ==
	 * 4, the behind-the-eye fixup - so a portal the classic walk refuses for
	 * any reason other than its parent's window is refused here too. */
	static void SetRoomBoundsReach(const Tr5Level::PortalRecord& p, int rn,
		Room& actualRoom, const float* mx, int fromRoom)
	{
		Room& r = g_room[(size_t)rn];
		float vx[4], vy[4], vz[4];
		float x, y, z, tooNear;
		float rawL = 1e30f, rawR = -1e30f, rawT = 1e30f, rawB = -1e30f;
		float cL, cR, cT, cB;			// the classic window
		float aL, aR, aT, aB;			// the reach

		/* The classic early out, and it may only be taken when the reach has
		 * nothing to add either - that is, when this room has been given its
		 * one reach visit already. */
		if (!r.reach_first && r.left <= actualRoom.test_left &&
			r.right >= actualRoom.test_right &&
			r.top <= actualRoom.test_top &&
			r.bottom >= actualRoom.test_bottom)
		{
			Note(fromRoom, rn, "covered", r.left, r.top, r.right, r.bottom);
			return;
		}

		tooNear = 0;

		for (int i = 0; i < 4; i++)
		{
			const float dx = (float)p.v[i][0];
			const float dy = (float)p.v[i][1];
			const float dz = (float)p.v[i][2];

			vx[i] = mx[0] * dx + mx[1] * dy + mx[2] * dz + mx[3];
			vy[i] = mx[4] * dx + mx[5] * dy + mx[6] * dz + mx[7];
			vz[i] = mx[8] * dx + mx[9] * dy + mx[10] * dz + mx[11];
			x = vx[i];
			y = vy[i];
			z = vz[i];

			if (z <= 0)
				tooNear++;
			else
			{
				z /= g_persp;

				if (z)
				{
					x = x / z + g_centerx;
					y = y / z + g_centery;
				}
				else
				{
					x = x < 0 ? 0.0f : (float)g_winxmax;
					y = y < 0 ? 0.0f : (float)g_winymax;
				}

				if (x - 1 < rawL) rawL = x - 1;
				if (x + 1 > rawR) rawR = x + 1;
				if (y - 1 < rawT) rawT = y - 1;
				if (y + 1 > rawB) rawB = y + 1;
			}
		}

		if (tooNear == 4)
		{
			Note(fromRoom, rn, "toonear", 4, 0, 0, 0);
			return;
		}

		/* The classic rectangle is the seeded loop of the classic function,
		 * written as the minimum and maximum it really computes: tL is the
		 * smallest of the parent's right edge and every (x - 1), and a
		 * minimum of the same floats is the same float in any order. */
		cL = rawL < (float)actualRoom.test_right ? rawL : (float)actualRoom.test_right;
		cR = rawR > (float)actualRoom.test_left ? rawR : (float)actualRoom.test_left;
		cT = rawT < (float)actualRoom.test_bottom ? rawT : (float)actualRoom.test_bottom;
		cB = rawB > (float)actualRoom.test_top ? rawB : (float)actualRoom.test_top;
		aL = rawL;
		aR = rawR;
		aT = rawT;
		aB = rawB;

		if (tooNear > 0)
		{
			for (int i = 0, last = 3; i < 4; last = i, i++)
			{
				if ((vz[last] <= 0) == (vz[i] <= 0))
					continue;

				if (vx[i] < 0 && vx[last] < 0)
					cL = aL = 0;
				else if (vx[i] > 0 && vx[last] > 0)
					cR = aR = (float)g_winxmax;
				else
				{
					cL = aL = 0;
					cR = aR = (float)g_winxmax;
				}

				if (vy[i] < 0 && vy[last] < 0)
					cT = aT = 0;
				else if (vy[i] > 0 && vy[last] > 0)
					cB = aB = (float)g_winymax;
				else
				{
					cT = aT = 0;
					cB = aB = (float)g_winymax;
				}
			}
		}

		if (cL < actualRoom.test_left) cL = actualRoom.test_left;
		if (cR > actualRoom.test_right) cR = actualRoom.test_right;
		if (cT < actualRoom.test_top) cT = actualRoom.test_top;
		if (cB > actualRoom.test_bottom) cB = actualRoom.test_bottom;

		// The reach against the screen, and against nothing else.
		if (aL < 0) aL = 0;
		if (aR > (float)g_winxmax) aR = (float)g_winxmax;
		if (aT < 0) aT = 0;
		if (aB > (float)g_winymax) aB = (float)g_winymax;

		/* Off the screen altogether. The classic rectangle cannot survive
		 * this either: it is clamped against the parent's window, which is
		 * inside the screen. */
		if (aL >= aR || aT >= aB)
		{
			Note(fromRoom, rn, "offscreen", aL, aT, aR, aB);
			return;
		}

		// The classic window only exists under a parent that has one.
		const bool classicOk = actualRoom.classic_ok && cL < cR && cT < cB;

		if (!classicOk && !r.reach_first)
		{
			Note(fromRoom, rn, "covered", aL, aT, aR, aB);
			return;
		}

		// g_reach_box: set by the first visit, widened by every later one.
		if (r.reach_first)
		{
			r.box[0] = (short)aL;
			r.box[1] = (short)aT;
			r.box[2] = (short)aR;
			r.box[3] = (short)aB;
		}
		else
		{
			if ((short)aL < r.box[0]) r.box[0] = (short)aL;
			if ((short)aT < r.box[1]) r.box[1] = (short)aT;
			if ((short)aR > r.box[2]) r.box[2] = (short)aR;
			if ((short)aB > r.box[3]) r.box[3] = (short)aB;
		}

		if (r.bound_active & 2)
		{
			if (aL < r.test_reach_left) r.test_reach_left = (short)aL;
			if (aT < r.test_reach_top) r.test_reach_top = (short)aT;
			if (aR > r.test_reach_right) r.test_reach_right = (short)aR;
			if (aB > r.test_reach_bottom) r.test_reach_bottom = (short)aB;

			if (classicOk)
			{
				if (r.classic_ok)
				{
					if (cL < r.test_left) r.test_left = (short)cL;
					if (cT < r.test_top) r.test_top = (short)cT;
					if (cR > r.test_right) r.test_right = (short)cR;
					if (cB > r.test_bottom) r.test_bottom = (short)cB;
				}
				else
				{
					r.test_left = (short)cL;
					r.test_right = (short)cR;
					r.test_top = (short)cT;
					r.test_bottom = (short)cB;
					r.classic_ok = true;
				}
			}

			r.reach_first = false;
			Note(fromRoom, rn, classicOk ? "widened" : "widened_reach",
				aL, aT, aR, aB);
			return;
		}

		g_list[g_end % kRing] = rn;
		g_end++;

		if (g_end - g_start > g_queue_peak)
			g_queue_peak = g_end - g_start;

		r.bound_active |= 2;
		r.reach_first = false;
		r.test_reach_left = (short)aL;
		r.test_reach_right = (short)aR;
		r.test_reach_top = (short)aT;
		r.test_reach_bottom = (short)aB;
		r.classic_ok = classicOk;

		if (classicOk)
		{
			r.test_left = (short)cL;
			r.test_right = (short)cR;
			r.test_top = (short)cT;
			r.test_bottom = (short)cB;
		}

		Note(fromRoom, rn, classicOk ? "reached" : "reach", aL, aT, aR, aB);
	}

	/* -vis 1: the visibility path's walk (game\draw.cpp, VisExtend and
	 * VisFollowPortal) over the tables hd_vis.cpp builds out of the .TRG and
	 * the .TRC (LoadHd), with the reach off. `g_vis_sources` (-vissrc) lets
	 * the walk ask the three tables one by one: bit 0 the guests, bit 1 the
	 * openings, bit 2 the wide portals. `g_vis_why` is per room the source
	 * that queued it first, as VISLOG prints it: 0 the portals or the seed,
	 * 1 a guest, 2 an opening, 3 a wide portal, 4 a portal at the window's
	 * edge, 0xff not queued. */
	static bool g_vis = false;
	static int g_vis_sources = 7;
	static HDVisParams g_vis_params = HDVisDefaults();
	static HDVisTables g_vis_tables;
	static std::vector<HDVisRoom> g_vis_rooms;
	static std::vector<HDCoverFace> g_vis_faces;
	static std::vector<HDVisPortal> g_vis_portals;
	static std::vector<int> g_vis_links;
	static std::vector<unsigned char> g_vis_why;
	static int g_vis_count[6] = { 0, 0, 0, 0, 0, 0 };

	// The group a room holds now, as hd_level.cpp's g_room_group.
	static int VisContents(int room)
	{
		return room >= 0 && (size_t)room < g_group_of.size()
			? g_group_of[(size_t)room] : room;
	}

	// HDVisRoomOfGroup (hd_level.cpp) over -flip's mapping.
	static int VisRoomOfGroup(int group)
	{
		if (group < 0 || (size_t)group >= g_room.size())
			return -1;

		const int main = (size_t)group < g_main_of.size()
			? g_main_of[(size_t)group] : -1;

		if (main < 0)
			return group;

		return g_group_of[(size_t)main] == group ? main : -1;
	}

	static void VisNote(int rn, int why, char before)
	{
		if (rn < 0 || (size_t)rn >= g_vis_why.size() || (before & 3) ||
			!(g_room[(size_t)rn].bound_active & 2) ||
			g_vis_why[(size_t)rn] != 0xff)
			return;

		g_vis_why[(size_t)rn] = (unsigned char)why;
		g_vis_count[why]++;
	}

	// VisQueue (game\draw.cpp).
	static void VisQueue(int rn, Room& actualRoom, float tL, float tT,
		float tR, float tB, int fromRoom, bool toScreen = false)
	{
		Room& r = g_room[(size_t)rn];
		const float l = toScreen ? 0.0f : (float)actualRoom.test_left;
		const float t = toScreen ? 0.0f : (float)actualRoom.test_top;
		const float rr = toScreen ? (float)g_winxmax :
			(float)actualRoom.test_right;
		const float b = toScreen ? (float)g_winymax :
			(float)actualRoom.test_bottom;

		if (tL < l) tL = l;
		if (tR > rr) tR = rr;
		if (tT < t) tT = t;
		if (tB > b) tB = b;

		if (tL >= tR || tT >= tB)
		{
			Note(fromRoom, rn, "empty", tL, tT, tR, tB);
			return;
		}

		if (r.left <= tL && r.right >= tR && r.top <= tT && r.bottom >= tB)
		{
			Note(fromRoom, rn, "covered", r.left, r.top, r.right, r.bottom);
			return;
		}

		if (r.bound_active & 2)
		{
			if (tL < r.test_left) r.test_left = (short)tL;
			if (tT < r.test_top) r.test_top = (short)tT;
			if (tR > r.test_right) r.test_right = (short)tR;
			if (tB > r.test_bottom) r.test_bottom = (short)tB;
			Note(fromRoom, rn, "widened", tL, tT, tR, tB);
			return;
		}

		g_list[g_end % kRing] = rn;
		g_end++;

		if (g_end - g_start > g_queue_peak)
			g_queue_peak = g_end - g_start;

		r.bound_active |= 2;
		r.test_left = (short)tL;
		r.test_right = (short)tR;
		r.test_top = (short)tT;
		r.test_bottom = (short)tB;
		Note(fromRoom, rn, "reached", tL, tT, tR, tB);
	}

	// VisGuestBounds (game\draw.cpp).
	static void VisGuestBounds(const HDVisGuest& guest, int rn, Room& r,
		const float* mx, int fromRoom)
	{
		float tL = 1e30f, tR = -1e30f, tT = 1e30f, tB = -1e30f;
		int behind = 0;

		for (int c = 0; c < 8; c++)
		{
			const float lx = ((c & 1) ? guest.hi[0] : guest.lo[0]) - (float)r.x;
			const float ly = ((c & 2) ? guest.hi[1] : guest.lo[1]) - (float)r.y;
			const float lz = ((c & 4) ? guest.hi[2] : guest.lo[2]) - (float)r.z;
			float x = mx[0] * lx + mx[1] * ly + mx[2] * lz + mx[3];
			float y = mx[4] * lx + mx[5] * ly + mx[6] * lz + mx[7];
			float z = mx[8] * lx + mx[9] * ly + mx[10] * lz + mx[11];

			if (z <= 0)
			{
				behind++;
				continue;
			}

			z /= g_persp;

			if (!z)
			{
				behind++;
				continue;
			}

			x = x / z + g_centerx;
			y = y / z + g_centery;

			if (x - 1 < tL) tL = x - 1;
			if (x + 1 > tR) tR = x + 1;
			if (y - 1 < tT) tT = y - 1;
			if (y + 1 > tB) tB = y + 1;
		}

		if (behind == 8)
		{
			Note(fromRoom, rn, "toonear", 8, 0, 0, 0);
			return;
		}

		if (behind)
		{
			tL = 0;
			tT = 0;
			tR = (float)g_winxmax;
			tB = (float)g_winymax;
		}

		VisQueue(rn, r, tL, tT, tR, tB, fromRoom);
	}

	// A portal record out of an opening's or a wide portal's corners.
	static Tr5Level::PortalRecord VisRecord(int to, const short n[3],
		const short v[4][3])
	{
		Tr5Level::PortalRecord p;

		p.toRoom = to;
		p.nx = n[0];
		p.ny = n[1];
		p.nz = n[2];

		for (int c = 0; c < 4; c++)
			for (int k = 0; k < 3; k++)
				p.v[c][k] = v[c][k];

		return p;
	}

	// -visnear 0 turns VisNearPortals off, to measure without it.
	static bool g_vis_near = true;

	// VisNearPortals (game\draw.cpp).
	static void VisNearPortals(int here, Room& r)
	{
		const float reach = (float)g_vis_params.reach;
		const float e[3] = { (float)(g_eye[0] - r.x), (float)(g_eye[1] - r.y),
			(float)(g_eye[2] - r.z) };

		for (size_t d = 0; g_vis_near && d < r.doors.size(); d++)
		{
			const Tr5Level::PortalRecord& p = r.doors[d];
			const int to = p.toRoom;
			bool inside = true;

			if (to < 0 || (size_t)to >= g_room.size() || to == here)
				continue;

			for (int k = 0; k < 3 && inside; k++)
			{
				float lo = (float)p.v[0][k], hi = lo;

				for (int c = 1; c < 4; c++)
				{
					if ((float)p.v[c][k] < lo) lo = (float)p.v[c][k];
					if ((float)p.v[c][k] > hi) hi = (float)p.v[c][k];
				}

				inside = e[k] >= lo - reach && e[k] <= hi + reach;
			}

			if (!inside)
				continue;

			const char before = g_room[(size_t)to].bound_active;

			VisQueue(to, r, 0.0f, 0.0f, (float)g_winxmax, (float)g_winymax,
				here);
			VisNote(to, 5, before);
		}
	}

	// VisExtend (game\draw.cpp).
	static void VisExtend(int here, Room& r, const float* mx)
	{
		const int c = VisContents(here);
		const HDVisTables& t = g_vis_tables;

		VisNearPortals(here, r);

		if (c < 0 || c >= t.rooms)
			return;

		for (int k = 0; (g_vis_sources & 1) && k < t.guestCount[(size_t)c]; k++)
		{
			const HDVisGuest& g =
				t.guests[(size_t)(t.guestFirst[(size_t)c] + k)];
			const int b = VisRoomOfGroup(g.group);

			if (b < 0 || (size_t)b >= g_room.size() || b == here)
				continue;

			const char before = g_room[(size_t)b].bound_active;

			VisGuestBounds(g, b, r, mx, here);
			VisNote(b, 1, before);
		}

		for (int k = 0; (g_vis_sources & 2) && k < t.openCount[(size_t)c]; k++)
		{
			const HDVisOpening& o =
				t.openings[(size_t)(t.openFirst[(size_t)c] + k)];
			const float ex = (float)(g_eye[0] - r.x);
			const float ey = (float)(g_eye[1] - r.y);
			const float ez = (float)(g_eye[2] - r.z);
			const float side = o.n[0] * ((float)o.v[0][0] - ex) +
				o.n[1] * ((float)o.v[0][1] - ey) +
				o.n[2] * ((float)o.v[0][2] - ez);

			if (side >= 0 || o.behind < 0 ||
				(size_t)o.behind >= g_room.size() || o.behind == here)
				continue;

			const char before = g_room[(size_t)o.behind].bound_active;

			SetRoomBounds(VisRecord(o.behind, o.n, o.v), o.behind, r, mx, here);
			VisNote(o.behind, 2, before);
		}
	}

	// VisFollowPortal (game\draw.cpp): `d` is the portal's index in the room.
	// VisPortalRect (game\draw.cpp).
	static bool VisPortalRect(const Tr5Level::PortalRecord& p, const float* mx,
		float* rect)
	{
		float vx[4], vy[4], vz[4];
		int tooNear = 0;

		rect[0] = rect[1] = 1e30f;
		rect[2] = rect[3] = -1e30f;

		for (int i = 0; i < 4; i++)
		{
			const float dx = (float)p.v[i][0];
			const float dy = (float)p.v[i][1];
			const float dz = (float)p.v[i][2];

			vx[i] = mx[0] * dx + mx[1] * dy + mx[2] * dz + mx[3];
			vy[i] = mx[4] * dx + mx[5] * dy + mx[6] * dz + mx[7];
			vz[i] = mx[8] * dx + mx[9] * dy + mx[10] * dz + mx[11];

			if (vz[i] <= 0)
			{
				tooNear++;
				continue;
			}

			const float z = vz[i] / g_persp;
			float x = vx[i] < 0 ? 0.0f : (float)g_winxmax;
			float y = vy[i] < 0 ? 0.0f : (float)g_winymax;

			if (z)
			{
				x = vx[i] / z + g_centerx;
				y = vy[i] / z + g_centery;
			}

			if (x - 1 < rect[0]) rect[0] = x - 1;
			if (y - 1 < rect[1]) rect[1] = y - 1;
			if (x + 1 > rect[2]) rect[2] = x + 1;
			if (y + 1 > rect[3]) rect[3] = y + 1;
		}

		if (tooNear == 4)
			return false;

		for (int i = 0, last = 3; tooNear && i < 4; last = i, i++)
		{
			if ((vz[last] <= 0) == (vz[i] <= 0))
				continue;

			if (!(vx[i] > 0 && vx[last] > 0))
				rect[0] = 0;

			if (!(vx[i] < 0 && vx[last] < 0))
				rect[2] = (float)g_winxmax;

			if (!(vy[i] > 0 && vy[last] > 0))
				rect[1] = 0;

			if (!(vy[i] < 0 && vy[last] < 0))
				rect[3] = (float)g_winymax;
		}

		return true;
	}

	static void VisFollow(const Tr5Level::PortalRecord& p, size_t d, int to,
		Room& r, const float* mx, int fromRoom)
	{
		char before = g_room[(size_t)to].bound_active;
		Tr5Level::PortalRecord open = p;
		bool wide = false;
		float rect[4];

		SetRoomBounds(p, to, r, mx, fromRoom);
		VisNote(to, 0, before);

		const int c = VisContents(fromRoom);
		const HDVisTables& t = g_vis_tables;
		const int wides = (g_vis_sources & 4) && c >= 0 && c < t.rooms ?
			t.wideCount[(size_t)c] : 0;

		for (int k = 0; k < wides; k++)
		{
			const HDVisWide& w = t.wides[(size_t)(t.wideFirst[(size_t)c] + k)];

			if (w.portal != (int)d)
				continue;

			const short n[3] = { (short)p.nx, (short)p.ny, (short)p.nz };

			open = VisRecord(to, n, w.v);
			wide = true;
			before = g_room[(size_t)to].bound_active;
			SetRoomBounds(open, to, r, mx, fromRoom);
			VisNote(to, 3, before);
			break;
		}

		if (!VisPortalRect(open, mx, rect))
			return;

		if (rect[0] < r.test_right && rect[2] > r.test_left &&
			rect[1] < r.test_bottom && rect[3] > r.test_top)
			return;

		if (wide)
		{
			before = g_room[(size_t)to].bound_active;
			VisQueue(to, r, rect[0], rect[1], rect[2], rect[3], fromRoom, true);
			VisNote(to, 3, before);
			return;
		}

		// The window's edge (VisFollowPortal, game\draw.cpp).
		const float e = (float)(g_vis_params.edge * (double)(g_winxmax + 1));
		const float l = (float)r.test_left - e;
		const float tt = (float)r.test_top - e;
		const float rr = (float)r.test_right + e;
		const float b = (float)r.test_bottom + e;

		if (e <= 0.0f || rect[0] >= rr || rect[2] <= l || rect[1] >= b ||
			rect[3] <= tt)
			return;

		before = g_room[(size_t)to].bound_active;
		VisQueue(to, r, rect[0] > l ? rect[0] : l, rect[1] > tt ? rect[1] : tt,
			rect[2] < rr ? rect[2] : rr, rect[3] < b ? rect[3] : b, fromRoom,
			true);
		VisNote(to, 4, before);
	}

	// Which of the two SetRoomBounds this run is walking with.
	static void Follow(const Tr5Level::PortalRecord& p, int to, Room& r,
		const float* mx, int fromRoom)
	{
		if (g_vis)
		{
			VisFollow(p, (size_t)(&p - &r.doors[0]), to, r, mx, fromRoom);
			return;
		}

		if (g_reach)
			SetRoomBoundsReach(p, to, r, mx, fromRoom);
		else
			SetRoomBounds(p, to, r, mx, fromRoom);
	}

	/* GetRoomBounds (game\draw.cpp). ROOM_OUTSIDE and the snow flag are
	 * left out - they change no room list. */
	static void GetRoomBounds()
	{
		while (g_start != g_end)
		{
			const long rn = g_list[g_start % kRing];
			float mx[12];

			g_start++;

			if (++g_dequeues > kWalkMax)
			{
				g_capped = true;
				break;
			}

			Room& r = g_room[(size_t)rn];
			r.bound_active -= 2;

			/* The reach is accumulated exactly as the classic window is, and
			 * the classic one only when this dequeue had one. A room the
			 * reach alone brought in keeps the wiped window PrintObjects
			 * leaves behind - left > right - which is the empty scissor the
			 * HD path reads. */
			if (g_reach)
			{
				if (r.test_reach_left < r.reach_left) r.reach_left = r.test_reach_left;
				if (r.test_reach_top < r.reach_top) r.reach_top = r.test_reach_top;
				if (r.test_reach_right > r.reach_right) r.reach_right = r.test_reach_right;
				if (r.test_reach_bottom > r.reach_bottom) r.reach_bottom = r.test_reach_bottom;
			}

			if (!g_reach || r.classic_ok)
			{
				if (r.test_left < r.left) r.left = r.test_left;
				if (r.test_top < r.top) r.top = r.test_top;
				if (r.test_right > r.right) r.right = r.test_right;
				if (r.test_bottom > r.bottom) r.bottom = r.test_bottom;
			}

			if (!(r.bound_active & 1))
			{
				g_drawn.push_back((short)rn);
				r.bound_active |= 1;
			}

			// phd_TranslateAbs(r->x, r->y, r->z) on top of the eye's rotation.
			{
				const float fx = (float)(r.x - g_eye[0]);
				const float fy = (float)(r.y - g_eye[1]);
				const float fz = (float)(r.z - g_eye[2]);

				for (int row = 0; row < 3; row++)
				{
					mx[row * 4 + 0] = g_view[row * 3 + 0];
					mx[row * 4 + 1] = g_view[row * 3 + 1];
					mx[row * 4 + 2] = g_view[row * 3 + 2];
					mx[row * 4 + 3] = fx * g_view[row * 3 + 0] +
						fy * g_view[row * 3 + 1] + fz * g_view[row * 3 + 2];
				}
			}

			for (size_t d = 0; d < r.doors.size(); d++)
			{
				const Tr5Level::PortalRecord& p = r.doors[d];
				const int to = p.toRoom;

				if (to < 0 || to >= (int)g_room.size())
					continue;

				if (p.nx)
				{
					const long x = r.x + p.v[0][0] - g_eye[0];

					if (x && (x ^ p.nx) < 0)
						Follow(p, to, r, mx, (int)rn);
					else
					{
						Note((int)rn, to, "side", (double)x, p.nx, 0, 0);
						continue;
					}
				}

				if (p.ny)
				{
					const long y = r.y + p.v[0][1] - g_eye[1];

					if (y && (y ^ p.ny) < 0)
						Follow(p, to, r, mx, (int)rn);
					else
					{
						Note((int)rn, to, "side", (double)y, p.ny, 1, 0);
						continue;
					}
				}

				if (p.nz)
				{
					const long z = r.z + p.v[0][2] - g_eye[2];

					if (z && (z ^ p.nz) < 0)
						Follow(p, to, r, mx, (int)rn);
					else
					{
						Note((int)rn, to, "side", (double)z, p.nz, 2, 0);
						continue;
					}
				}
			}

			if (g_vis)
				VisExtend((int)rn, r, mx);
		}
	}

	static bool Load(const std::string& folder, const char* stem)
	{
		Tr5Level level;
		std::string error;
		const std::string trc = folder + "\\" + std::string(stem) + ".TRC";
		std::wstring wide;

		for (size_t i = 0; i < trc.size(); i++)
			wide.push_back((wchar_t)(unsigned char)trc[i]);

		if (!level.load(wide, error))
		{
			printf("-walk: could not read %s (%s)\n", trc.c_str(), error.c_str());
			return false;
		}

		g_room.resize((size_t)level.roomCount());

		for (int i = 0; i < level.roomCount(); i++)
		{
			const Tr5Room& rm = level.rooms()[(size_t)i];
			Room& r = g_room[(size_t)i];
			unsigned yOrigin = 0;

			level.headerField(i, 24, yOrigin, error);
			r.x = rm.originX;
			r.y = (long)(int)yOrigin;
			r.z = rm.originZ;
			r.xs = rm.numXSectors;
			r.zs = rm.numZSectors;

			{
				unsigned yTop = 0;

				level.headerField(i, 36, yTop, error);
				r.ceiling = (long)(int)yTop;
				r.floor = rm.yBottom;
			}
			r.flags = rm.flags;
			r.alt = rm.altRoom;
			level.portalRecords(i, r.doors, error);

			for (size_t k = 0; (g_portal_room == -1 || g_portal_room == i) &&
				k < r.doors.size(); k++)
			{
				const Tr5Level::PortalRecord& q = r.doors[k];

				printf("-visportals: room %d portal %d to %d normal %d %d %d",
					i, (int)k, q.toRoom, q.nx, q.ny, q.nz);

				for (int c = 0; c < 4; c++)
					printf(" %ld,%d,%ld", r.x + q.v[c][0], q.v[c][1],
						r.z + q.v[c][2]);

				printf("\n");
			}
		}

		/* The closed doors: the sector in front of the door names the room
		 * behind it (GetDoor), and in each of the two rooms the portal whose
		 * outline, rounded to the quarter sector, holds the point half a
		 * sector past the door is the one it shuts - the last such portal,
		 * as SCDS keeps it. */
		g_doors.clear();

		for (int i = 0; i < level.itemCount(); i++)
		{
			Tr5Item item;

			if (!level.getItem(i, item) || item.objectNumber < kClosedDoor1 ||
				item.objectNumber > kLiftDoors2 || item.roomNumber < 0 ||
				item.roomNumber >= level.roomCount())
				continue;

			const Tr5Room& rm = level.rooms()[(size_t)item.roomNumber];
			long dx = 0, dy = 0;

			if (!item.yRot)
				dx = -1;
			else if (item.yRot == -0x8000)
				dx = 1;
			else if (item.yRot == 0x4000)
				dy = -1;
			else
				dy = 1;

			const long sx = ((item.x - rm.originX) >> 10) + dy;
			const long sz = ((item.z - rm.originZ) >> 10) + dx;
			std::vector<Tr5Level::SectorInfo> sectors;
			int behind = -1;

			if (level.sectorInfo(item.roomNumber, sectors, error))
				for (size_t s = 0; s < sectors.size(); s++)
					if (sectors[s].sx == sx && sectors[s].sz == sz)
						behind = sectors[s].doorTo;

			if (behind < 0 || behind >= level.roomCount())
				continue;

			DoorPortals door;
			const long ox = item.x + (dy << 10);
			const long oz = item.z + (dx << 10);
			const int both[2] = { item.roomNumber, behind };

			door.item = i;

			for (int k = 0; k < 2; k++)
			{
				const Room& r = g_room[(size_t)both[k]];
				int found = -1;

				for (size_t p = 0; p < r.doors.size(); p++)
				{
					const Tr5Level::PortalRecord& q = r.doors[p];
					long minx = r.x + ((dy << 10) >> 1) + ((q.v[0][0] + 128) & ~255L);
					long maxx = r.x + ((dy << 10) >> 1) + ((q.v[2][0] + 128) & ~255L);
					long minz = r.z + ((dx << 10) >> 1) + ((q.v[0][2] + 128) & ~255L);
					long maxz = r.z + ((dx << 10) >> 1) + ((q.v[2][2] + 128) & ~255L);

					if (minx > maxx)
					{
						const long t = minx;
						minx = maxx;
						maxx = t;
					}

					if (minz > maxz)
					{
						const long t = minz;
						minz = maxz;
						maxz = t;
					}

					if (ox >= minx && ox <= maxx && oz >= minz && oz <= maxz)
						found = (int)p;
				}

				if (found >= 0)
					door.portals.push_back(std::make_pair(both[k], found));
			}

			if (!door.portals.empty())
				g_doors.push_back(door);

			for (size_t k = 0; g_door_print && k < door.portals.size(); k++)
			{
				const Room& r = g_room[(size_t)door.portals[k].first];
				const Tr5Level::PortalRecord& q =
					r.doors[(size_t)door.portals[k].second];

				printf("-walkdoors: item %d slot %d room %d at %d %d %d "
					"yrot %d behind %d shuts room %d portal %d to %d",
					i, item.objectNumber, item.roomNumber, (int)item.x,
					(int)item.y, (int)item.z, item.yRot, behind,
					door.portals[k].first, door.portals[k].second, q.toRoom);

				for (int c = 0; c < 4; c++)
					printf(" %ld,%d,%ld", r.x + q.v[c][0], q.v[c][1],
						r.z + q.v[c][2]);

				printf("\n");
			}
		}

		g_group_of.resize(g_room.size());
		g_main_of.assign(g_room.size(), -1);

		for (size_t i = 0; i < g_room.size(); i++)
		{
			g_group_of[i] = (int)i;

			if (g_room[i].alt >= 0 && g_room[i].alt < (int)g_room.size())
			{
				g_main_of[i] = (int)i;
				g_main_of[(size_t)g_room[i].alt] = (int)i;
			}
		}

		printf("-walk: %d rooms out of %s, %d closed door(s) with a portal\n",
			(int)g_room.size(), trc.c_str(), (int)g_doors.size());

		if (g_vis)
			VisInputFromLevel(level, g_vis_rooms, g_vis_faces, g_vis_portals,
				g_vis_links);

		return true;
	}

	/* Shuts the portals of the doors whose flag is set in `shut`, the way
	 * ShutThatDoor does: all three normals zero. */
	static void ShutDoors(const std::vector<unsigned char>& shut)
	{
		for (size_t d = 0; d < g_doors.size() && d < shut.size(); d++)
			if (shut[d])
				for (size_t k = 0; k < g_doors[d].portals.size(); k++)
				{
					Room& r = g_room[(size_t)g_doors[d].portals[k].first];
					Tr5Level::PortalRecord& p =
						r.doors[(size_t)g_doors[d].portals[k].second];

					p.nx = p.ny = p.nz = 0;
				}
	}

	/* HDRoomIsHD as far as the .TRG alone answers it: a group with any
	 * triangle. A group the engine finds hollow and a room a randomizer
	 * marker turned classic are HD in this answer and not in the engine's;
	 * the log's HDLOG room= lines carry the engine's own, which -walkdump
	 * reads instead. */
	static bool LoadHd(const std::string& root, const char* stem)
	{
		trf::remaster::Trg trg;
		std::string error;
		const std::string path = root + "\\DATA\\" + std::string(stem) + ".TRG";
		int hd = 0;

		g_hd.clear();

		if (!trf::remaster::read_trg(path, trg, error))
		{
			printf("-walk: no .TRG at %s (%s) - no room is trimmed for want "
				"of art\n", path.c_str(), error.c_str());
			return false;
		}

		g_hd.assign(g_room.size(), 0);

		for (size_t g = 0; g < g_room.size() && g < trg.groups.size(); g++)
			for (size_t p = 0; p < trg.groups[g].parts.size() && !g_hd[g]; p++)
				for (int pass = 0; pass < 3; pass++)
					if (trg.groups[g].parts[p].pass[pass].count >= 3)
						g_hd[g] = 1;

		for (size_t g = 0; g < g_hd.size(); g++)
			hd += g_hd[g];

		printf("-walk: %d of %d rooms have HD art in %s\n", hd,
			(int)g_hd.size(), path.c_str());

		if (g_vis)
		{
			HDVisBuild(trg, g_vis_rooms.empty() ? 0 : &g_vis_rooms[0],
				(int)g_vis_rooms.size(),
				g_vis_faces.empty() ? 0 : &g_vis_faces[0],
				(int)g_vis_faces.size(),
				g_vis_portals.empty() ? 0 : &g_vis_portals[0],
				(int)g_vis_portals.size(),
				g_vis_links.empty() ? 0 : &g_vis_links[0],
				(int)g_vis_links.size(), g_vis_params, &g_vis_tables);

			const HDVisTables& t = g_vis_tables;

			printf("-vis: tables rooms=%d art_rooms=%d walls=%d bare=%d "
				"void=%d guests=%d guest_tris=%lld wide=%d ms=%.0f "
				"reach=%.0f bare_share=%.2f guest_tris_min=%d sources=%d "
				"twin=%d wide_bare=%.2f edge=%.5f mirror=%d\n",
				t.rooms, t.artRooms, t.wallFaces, t.bareFaces, t.voidFaces,
				t.guestPairs, t.guestTris, t.widePortals, t.ms,
				g_vis_params.reach, g_vis_params.bare, g_vis_params.guestTris,
				g_vis_sources, t.openingsTwin, g_vis_params.wideBare,
				g_vis_params.edge, t.wideMirrored);
		}

		/* The carried rooms, as CarryHostsOf (hd_render.cpp) counts
		 * them: a triangle whose centroid stands outside its own room's
		 * inside and inside the inside of a room under the sky that is not
		 * its flipmap twin counts for that pair, and a pair more than half
		 * of whose triangles another group has a copy of (HDCarryCopyKey)
		 * is none. */
		g_carry.assign(g_room.size(), std::vector<short>());

		if (!g_air_option)
			return true;

		std::map<std::pair<int, int>, int> pairs, copies;
		std::vector<std::pair<long long, int> > cents;

		// Every triangle's world centroid cell, for the copies.
		for (size_t g = 0; g < g_room.size() && g < trg.groups.size(); g++)
			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
				for (int pass = 0; pass < 3; pass++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];

					for (unsigned at = 0; at + 2 < run.count; at += 3)
					{
						const unsigned first = run.start + at;

						if (first + 2 >= trg.indices.size())
							break;

						const trf::remaster::TrgVertex& ta =
							trg.vertices[trg.indices[first]];
						const trf::remaster::TrgVertex& tb =
							trg.vertices[trg.indices[first + 1]];
						const trf::remaster::TrgVertex& tc =
							trg.vertices[trg.indices[first + 2]];

						cents.push_back(std::make_pair(HDCarryCopyKey(
							((float)ta.x + (float)tb.x + (float)tc.x) / 3.0f +
							(float)g_room[g].x,
							((float)ta.y + (float)tb.y + (float)tc.y) / 3.0f,
							((float)ta.z + (float)tb.z + (float)tc.z) / 3.0f +
							(float)g_room[g].z), (int)g));
					}
				}

		std::sort(cents.begin(), cents.end());

		for (size_t g = 0; g < g_room.size() && g < trg.groups.size(); g++)
		{
			const Room& own = g_room[g];

			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
				for (int pass = 0; pass < 3; pass++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];

					for (unsigned at = 0; at + 2 < run.count; at += 3)
					{
						const unsigned first = run.start + at;

						if (first + 2 >= trg.indices.size())
							break;

						const trf::remaster::TrgVertex& ta =
							trg.vertices[trg.indices[first]];
						const trf::remaster::TrgVertex& tb =
							trg.vertices[trg.indices[first + 1]];
						const trf::remaster::TrgVertex& tc =
							trg.vertices[trg.indices[first + 2]];
						float c[3] = {
							((float)ta.x + (float)tb.x + (float)tc.x) / 3.0f,
							((float)ta.y + (float)tb.y + (float)tc.y) / 3.0f,
							((float)ta.z + (float)tb.z + (float)tc.z) / 3.0f };
						HDRoomBox ob;

						ob.mins[0] = 1024.0f;
						ob.maxs[0] = (float)((own.xs - 1) << 10);
						ob.mins[1] = (float)(own.ceiling - own.y);
						ob.maxs[1] = (float)(own.floor - own.y);
						ob.mins[2] = 1024.0f;
						ob.maxs[2] = (float)((own.zs - 1) << 10);

						if (HDCarryBoxHolds(ob, c))
							continue;

						c[0] += (float)own.x;
						c[2] += (float)own.z;

						for (size_t a = 0; a < g_room.size(); a++)
						{
							const Room& host = g_room[a];

							if (a == g || !(host.flags & 0x8) ||
								host.alt == (int)g || own.alt == (int)a)
								continue;

							HDRoomBox hb;

							hb.mins[0] = (float)(host.x + 1024);
							hb.maxs[0] = (float)(host.x +
								((long)(host.xs - 1) << 10));
							hb.mins[1] = (float)(host.ceiling - host.y);
							hb.maxs[1] = (float)(host.floor - host.y);
							hb.mins[2] = (float)(host.z + 1024);
							hb.maxs[2] = (float)(host.z +
								((long)(host.zs - 1) << 10));

							if (!HDCarryBoxHolds(hb, c))
								continue;

							pairs[std::make_pair((int)a, (int)g)]++;

							const long long key = HDCarryCopyKey(c[0], c[1],
								c[2]);
							std::vector<std::pair<long long, int> >::
								const_iterator it = std::lower_bound(
								cents.begin(), cents.end(),
								std::make_pair(key, -1));

							for (; it != cents.end() && it->first == key; ++it)
								if (it->second != (int)g)
								{
									copies[std::make_pair((int)a, (int)g)]++;
									break;
								}
						}
					}
				}
		}

		for (std::map<std::pair<int, int>, int>::const_iterator it =
			pairs.begin(); it != pairs.end(); ++it)
		{
			if (copies[it->first] * 2 <= it->second)
				g_carry[(size_t)it->first.first].push_back(
					(short)it->first.second);

			printf("-walk: carry host=%d group=%d tris=%d copies=%d\n",
				it->first.first, it->first.second, it->second,
				copies[it->first]);
		}

		return true;
	}

	/* CarryArt (game\draw.cpp) over the last walk: every room with a
	 * classic window takes in, as a room with no window of its own, the
	 * rooms whose art stands in its inside, with its own reach. */
	static void CarryArt()
	{
		const size_t listed = g_drawn.size();

		g_carried = 0;

		for (size_t i = 0; i < listed; i++)
		{
			const Room& a = g_room[(size_t)g_drawn[i]];
			// HDRoomCarries: the list of the group the room draws now.
			const size_t key = (size_t)g_drawn[i] < g_group_of.size() &&
				g_group_of[(size_t)g_drawn[i]] >= 0
				? (size_t)g_group_of[(size_t)g_drawn[i]] : (size_t)g_drawn[i];

			if (a.left >= a.right || a.top >= a.bottom || key >= g_carry.size())
				continue;

			const std::vector<short>& carried = g_carry[key];

			for (size_t k = 0; k < carried.size(); k++)
			{
				// HDCarriedRoom: the room drawing that art in this state.
				const int main = (size_t)carried[k] < g_main_of.size()
					? g_main_of[(size_t)carried[k]] : -1;
				const int at = main < 0 ? carried[k]
					: g_group_of[(size_t)main] == carried[k] ? main : -1;

				if (at < 0)
					continue;

				Room& b = g_room[(size_t)at];
				bool held = false;

				for (size_t j = 0; j < g_drawn.size() && !held; j++)
					held = g_drawn[j] == at;

				if (held)
					continue;

				for (int e = 0; e < 4; e++)
					b.box[e] = a.box[e];

				g_drawn.push_back((short)at);
				g_carried++;
			}
		}
	}

	// One frame of the walk. Returns the room list.
	static void One(int seed, long ex, long ey, long ez, double yawDeg,
		double pitchDeg, long width, long height, double fov)
	{
		/* The three rows of the eye's rotation, aGenerateW2V's with no roll:
		 * right, down (y grows downwards), forward. Yaw 0 looks along +Z and
		 * grows towards +X, a positive pitch in degrees looks down - the
		 * engine's x_rot is negative there, which is why it is negated. */
		enginecam::Init();

		if (g_use_target)
		{
			short angles[2];

			enginecam::VectorAngles(g_target[0] - ex, g_target[1] - ey,
				g_target[2] - ez, angles);
			g_yrot = angles[0];
			g_xrot = angles[1];
		}
		else
		{
			g_yrot = enginecam::Angle(yawDeg);
			g_xrot = enginecam::Angle(-pitchDeg);
		}

		enginecam::Rotation(g_xrot, g_yrot, g_zrot, g_view);

		g_winxmax = (short)(width - 1);
		g_winymax = (short)(height - 1);
		g_centerx = (float)(width / 2);
		g_centery = (float)(height / 2);
		g_persp_units = enginecam::Persp(height,
			g_fov_units ? g_fov_units : enginecam::FovUnits(fov));
		g_persp = (float)g_persp_units;
		g_eye[0] = ex;
		g_eye[1] = ey;
		g_eye[2] = ez;

		for (size_t i = 0; i < g_room.size(); i++)
		{
			Room& r = g_room[i];

			// What PrintObjects (game\draw.cpp) leaves behind.
			r.left = g_winxmax;
			r.top = g_winymax;
			r.right = 0;
			r.bottom = 0;
			r.bound_active = 0;

			// And the reach, which the engine wipes for itself at the
			// top of the walk (nothing else in a frame writes it).
			r.reach_left = g_winxmax;
			r.reach_top = g_winymax;
			r.reach_right = 0;
			r.reach_bottom = 0;
			r.classic_ok = false;
			r.reach_first = true;
		}

		g_verdict.clear();
		g_drawn.clear();
		g_start = 0;
		g_end = 1;
		g_list[0] = seed;
		g_vis_why.assign(g_room.size(), 0xff);
		g_vis_count[0] = g_vis_count[1] = g_vis_count[2] = g_vis_count[3] =
			g_vis_count[4] = g_vis_count[5] = 0;
		g_queue_peak = 1;
		g_reach_only = 0;
		g_dequeues = 0;
		g_capped = false;

		{
			Room& r = g_room[(size_t)seed];

			r.test_left = 0;
			r.test_top = 0;
			r.test_right = g_winxmax;
			r.test_bottom = g_winymax;
			r.bound_active = 2;
			// The root's reach is the screen: every chain of portals that
			// faces the eye is followed to its end.
			r.test_reach_left = 0;
			r.test_reach_top = 0;
			r.test_reach_right = g_winxmax;
			r.test_reach_bottom = g_winymax;
			r.box[0] = 0;
			r.box[1] = 0;
			r.box[2] = g_winxmax;
			r.box[3] = g_winymax;
			r.classic_ok = true;
			r.reach_first = false;
		}

		GetRoomBounds();

		g_carried = 0;

		if (g_reach)
			CarryArt();

		// A room with no classic window at all: the reach alone brought it.
		if (g_reach)
			for (size_t i = 0; i < g_drawn.size(); i++)
			{
				const Room& r = g_room[(size_t)g_drawn[i]];

				if (r.left >= r.right || r.top >= r.bottom)
					g_reach_only++;
			}
	}

	// The distance from a point to a room's box, 0 inside it - the same
	// arithmetic as RoomDistance in game\draw.cpp, which is what the
	// ROOMPOP line measures with.
	static long BoxDistance(const Room& r, long x, long y, long z)
	{
		const long x0 = r.x, x1 = r.x + (long)r.xs * 1024;
		const long z0 = r.z, z1 = r.z + (long)r.zs * 1024;
		const long dx = x < x0 ? x0 - x : (x > x1 ? x - x1 : 0);
		const long dz = z < z0 ? z0 - z : (z > z1 ? z - z1 : 0);
		const long dy = y < r.ceiling ? r.ceiling - y
			: (y > r.floor ? y - r.floor : 0);

		return (long)sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz);
	}

	static bool Holds(int which)
	{
		for (size_t i = 0; i < g_drawn.size(); i++)
			if (g_drawn[i] == which)
				return true;

		return false;
	}

	/* ReachTrimRooms (game\draw.cpp) over the last walk: first the rooms
	 * the reach alone brought in that no HD group draws (g_hd), then,
	 * farthest first, rooms the reach alone brought in while the list is
	 * over the budget. -reachsurvey does not call it: it measures the walk's
	 * own list, which is what the budget was set from. */
	static std::vector<short> g_trimmed;		// what the last Trim took out

	static void Trim()
	{
		g_trim_noart = 0;
		g_trim_budget = 0;
		g_trimmed.clear();

		for (size_t i = 0; i < g_drawn.size(); )
		{
			const size_t rn = (size_t)g_drawn[i];
			const Room& r = g_room[rn];

			if ((r.left >= r.right || r.top >= r.bottom) && rn < g_hd.size() &&
				!g_hd[rn])
			{
				g_trimmed.push_back(g_drawn[i]);
				g_drawn.erase(g_drawn.begin() + (long)i);
				g_trim_noart++;
				continue;
			}

			i++;
		}

		while ((int)g_drawn.size() > kReachBudget)
		{
			int worst = -1;
			long worstDist = -1;

			for (size_t i = 0; i < g_drawn.size(); i++)
			{
				const Room& r = g_room[(size_t)g_drawn[i]];

				if (r.left < r.right && r.top < r.bottom)
					continue;

				const long d = BoxDistance(r, g_eye[0], g_eye[1], g_eye[2]);

				if (d > worstDist)
				{
					worstDist = d;
					worst = (int)i;
				}
			}

			if (worst < 0)
				break;

			g_trimmed.push_back(g_drawn[(size_t)worst]);
			g_drawn.erase(g_drawn.begin() + worst);
			g_trim_budget++;
		}
	}

	/* A listed room's answer after the whole walk: its classic window, the
	 * union of every rectangle it was dequeued with, which is what
	 * ROOMLIST prints as window= and HDFrameRoom is handed, and its reach=.
	 * REACH is a room with no classic window. */
	static void Final(int which, const char* lead)
	{
		const Room& r = g_room[(size_t)which];

		static const char* const kWhy[6] = { "walk", "box", "opening",
			"portal", "edge", "near" };
		const int why = g_vis && (size_t)which < g_vis_why.size() &&
			g_vis_why[(size_t)which] < 6 ? g_vis_why[(size_t)which] : 0;

		printf("%sroom %d final=%d,%d..%d,%d reach=%d,%d..%d,%d %s%s%s\n", lead,
			which, r.left, r.top, r.right, r.bottom, r.box[0], r.box[1],
			r.box[2], r.box[3],
			(r.left >= r.right || r.top >= r.bottom) ? "REACH" : "classic",
			g_vis ? " vis=" : "", g_vis ? kWhy[why] : "");
	}

	static void Why(int want)
	{
		if (Holds(want))
		{
			bool noted = false;

			/* The first note that put it in the list, which can be a reach
			 * note for a room a later portal gives a classic window - the
			 * final line after it is the room's answer. */
			for (size_t i = 0; i < g_verdict.size() && !noted; i++)
				if (g_verdict[i].to == want &&
					(!strcmp(g_verdict[i].why, "reached") ||
					!strcmp(g_verdict[i].why, "reach")))
				{
					printf("      room %d %s from room %d, window %.0f,%.0f..%.0f,%.0f\n",
						want, strcmp(g_verdict[i].why, "reach") ? "REACHED"
						: "REACH ONLY (no classic window)", g_verdict[i].from,
						g_verdict[i].n[0], g_verdict[i].n[1],
						g_verdict[i].n[2], g_verdict[i].n[3]);
					noted = true;
				}

			if (!noted)
				printf("      room %d is in the list (%s)\n", want,
					want == g_list[0] ? "seed" : "carried");

			Final(want, "      ");
			return;
		}

		for (size_t i = 0; i < g_trimmed.size(); i++)
			if (g_trimmed[i] == want)
				printf("      room %d TRIMMED - the reach alone brought it in "
					"and ReachTrimRooms took it out again (no HD art, or the "
					"budget of %d)\n", want, kReachBudget);

		int seen = 0;

		for (size_t i = 0; i < g_verdict.size(); i++)
			if (g_verdict[i].to == want)
			{
				printf("      room %d REFUSED from room %d by %-8s "
					"[%.0f %.0f %.0f %.0f]\n", want, g_verdict[i].from,
					g_verdict[i].why, g_verdict[i].n[0], g_verdict[i].n[1],
					g_verdict[i].n[2], g_verdict[i].n[3]);
				seen++;
			}

		if (!seen)
			printf("      room %d NOT REACHED - no portal to it out of any "
				"listed room\n", want);
	}
}

/* The whole of -walk, from the command line. Returns the process exit code. */
static int RunWalk(const std::string& folder, const char* stem, int seed,
	long ex, long ey, long ez, const double* yawFan, const double* pitchFan,
	int want, long width, long height, double fov, bool verbose, bool reach,
	bool centre, bool boxes, const std::string& root, const long* target,
	bool finals)
{
	if (!walkoff::Load(folder, stem))
		return 5;

	if (seed < 0 || seed >= (int)walkoff::g_room.size())
	{
		printf("-walk: room %d is not in this level\n", seed);
		return 5;
	}

	/* The reach's list is trimmed as the engine trims it, so the art is
	 * asked; -vis 1 builds its tables out of it, and walks with the reach
	 * off, as the engine does (game\draw.cpp, VisBeginWalk). */
	if (walkoff::g_vis)
		reach = false;

	if (reach || walkoff::g_vis)
		walkoff::LoadHd(root, stem);

	// -walkshut: those doors shut, every other one open.
	if (!walkoff::g_shut_items.empty())
	{
		std::vector<unsigned char> shut(walkoff::g_doors.size(), 0);
		int count = 0;

		for (size_t d = 0; d < walkoff::g_doors.size(); d++)
			for (size_t k = 0; k < walkoff::g_shut_items.size(); k++)
				if (walkoff::g_shut_items[k] < 0 ||
					walkoff::g_shut_items[k] == walkoff::g_doors[d].item)
					shut[d] = 1;

		for (size_t d = 0; d < shut.size(); d++)
			count += shut[d];

		walkoff::ShutDoors(shut);
		printf("-walkshut: %d door(s) shut\n", count);
	}

	/* -flip: FlipMap's three memcpy lines, after the doors are shut as the
	 * engine shuts them at load, before any flip - a flip carries a room's
	 * zeroed portals with it. A room with no twin is named and left. */
	for (size_t k = 0; k < walkoff::g_flip_rooms.size(); k++)
	{
		const int a = walkoff::g_flip_rooms[k];
		const int b = a >= 0 && a < (int)walkoff::g_room.size()
			? walkoff::g_room[(size_t)a].alt : -1;

		if (b < 0 || b >= (int)walkoff::g_room.size())
		{
			printf("-flip: room %d has no flipmap twin, not flipped\n", a);
			continue;
		}

		const walkoff::Room keep = walkoff::g_room[(size_t)a];

		walkoff::g_room[(size_t)a] = walkoff::g_room[(size_t)b];
		walkoff::g_room[(size_t)b] = keep;
		walkoff::g_room[(size_t)a].alt = b;
		walkoff::g_room[(size_t)b].alt = -1;
		std::swap(walkoff::g_group_of[(size_t)a],
			walkoff::g_group_of[(size_t)b]);
		printf("-flip: room=%d <-> room=%d now group=%d / group=%d\n", a, b,
			b, a);
	}

	/* -walktarget: one direction, the one phd_LookAt builds from the eye and
	 * the target, and the fan is not walked. */
	double oneYaw[3], onePitch[3];

	if (target)
	{
		walkoff::g_use_target = true;
		walkoff::g_target[0] = target[0];
		walkoff::g_target[1] = target[1];
		walkoff::g_target[2] = target[2];
		oneYaw[0] = oneYaw[1] = 0.0;
		oneYaw[2] = 1.0;
		onePitch[0] = onePitch[1] = 0.0;
		onePitch[2] = 1.0;
		yawFan = oneYaw;
		pitchFan = onePitch;
	}

	/* -walkcentre: the eye at the middle of the seed room's own box. The
	 * survey below needs an eye in every room of a level and there is no
	 * hand-picked one for 2,709 rooms; this is the same arithmetic
	 * BoxDistance uses, so "distance 0" and "the eye is inside this room"
	 * mean the same thing. */
	if (centre)
	{
		const walkoff::Room& r = walkoff::g_room[(size_t)seed];

		ex = r.x + (long)r.xs * 512;
		ez = r.z + (long)r.zs * 512;
		ey = (r.ceiling + r.floor) / 2;
		printf("-walkcentre: eye at the middle of room %d's box\n", seed);
	}

	{
		const walkoff::Room& r = walkoff::g_room[(size_t)seed];

		printf("-walk: seed room %d box x %ld..%ld  y %ld..%ld  z %ld..%ld\n",
			seed, r.x, r.x + (long)r.xs * 1024, r.ceiling, r.floor,
			r.z, r.z + (long)r.zs * 1024);
	}

	/* -walkboxes: every room's origin, which is what a -with argument is
	 * made of (the other room's origin less this one's). Printed on request
	 * because a picture of a whole room list is built out of it. */
	if (boxes)
		for (size_t i = 0; i < walkoff::g_room.size(); i++)
		{
			const walkoff::Room& r = walkoff::g_room[i];

			printf("-walkboxes room %3d origin %6ld %6ld %6ld  box x %ld..%ld "
				"y %ld..%ld z %ld..%ld\n", (int)i, r.x, r.y, r.z, r.x,
				r.x + (long)r.xs * 1024, r.ceiling, r.floor, r.z,
				r.z + (long)r.zs * 1024);
		}

	printf("-walk: rooms whose box holds the eye %ld/%ld/%ld:", ex, ey, ez);

	for (size_t i = 0; i < walkoff::g_room.size(); i++)
	{
		const walkoff::Room& r = walkoff::g_room[i];

		if (ex >= r.x && ex <= r.x + (long)r.xs * 1024 &&
			ez >= r.z && ez <= r.z + (long)r.zs * 1024)
			printf(" %d", (int)i);
	}

	printf("\n");

	printf("-walk: %s seed room %d, eye %ld/%ld/%ld, %ldx%ld, fov %.0f\n",
		stem, seed, ex, ey, ez, width, height, fov);

	std::vector<int> tally((size_t)walkoff::g_room.size(), 0);
	int frames = 0, wantIn = 0;
	int mostRooms = 0, fewestRooms = 1 << 30;

	/* What became of the wanted room over the whole fan. The widest window
	 * it ever gets is the number that matters - a room that only ever
	 * enters the picture as a sliver a few pixels wide at the rim is a room
	 * whose coming and going costs those pixels and nothing else. */
	int whyCount[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	double widest = 0, tallest = 0, narrowest = 1e9, shortest = 1e9;

	/* The two rules side by side. The classic walk is run first and its
	 * room count kept, then the same direction is walked again with the
	 * reach, so every line of the table below is one eye, one direction and
	 * two rules. */
	long sumClassic = 0, sumReach = 0;
	/* The two budgets, the old one and the new one, because what a named
	 * eye is asked for now is what the safety valve does to it. These are
	 * the walk's own list sizes, before the engine's no-art trim. */
	int mostClassic = 0, mostReach = 0, over64 = 0, over128 = 0;
	int reachOnlyWorst = 0;
	long queuePeak = 0, worstDequeues = 0;
	int capped = 0;
	double bestYaw = 0, bestPitch = 0;
	int bestGain = -1, bestClassic = 0, bestReach = 0;

	for (double pitch = pitchFan[0]; pitch <= pitchFan[1] + 1e-9;
		pitch += pitchFan[2] > 0 ? pitchFan[2] : 1.0)
	for (double yaw = yawFan[0]; yaw <= yawFan[1] + 1e-9; yaw += yawFan[2])
	{
		int classicRooms = 0;

		if (reach)
		{
			walkoff::g_reach = false;
			walkoff::One(seed, ex, ey, ez, yaw, pitch, width, height, fov);
			classicRooms = (int)walkoff::g_drawn.size();
			walkoff::g_reach = true;
		}

		walkoff::One(seed, ex, ey, ez, yaw, pitch, width, height, fov);
		frames++;

		if (reach)
		{
			const int now = (int)walkoff::g_drawn.size();

			sumClassic += classicRooms;
			sumReach += now;

			if (classicRooms > mostClassic) mostClassic = classicRooms;
			if (now > mostReach) mostReach = now;
			if (now > 64) over64++;
			if (now > 128) over128++;

			if (walkoff::g_reach_only > reachOnlyWorst)
				reachOnlyWorst = walkoff::g_reach_only;

			if (walkoff::g_queue_peak > queuePeak)
				queuePeak = walkoff::g_queue_peak;

			if (walkoff::g_dequeues > worstDequeues)
				worstDequeues = walkoff::g_dequeues;

			if (walkoff::g_capped)
				capped++;

			if (now - classicRooms > bestGain)
			{
				bestGain = now - classicRooms;
				bestYaw = yaw;
				bestPitch = pitch;
				bestClassic = classicRooms;
				bestReach = now;
			}

			// The list the engine hands the frame.
			walkoff::Trim();
		}

		if (target && frames == 1)
			printf("-walktarget: target %ld/%ld/%ld  y_rot %d x_rot %d "
				"(yaw %.4f pitch %.4f deg)  persp %ld\n", target[0],
				target[1], target[2], (int)walkoff::g_yrot,
				(int)walkoff::g_xrot, walkoff::g_yrot * 360.0 / 65536.0,
				-walkoff::g_xrot * 360.0 / 65536.0, walkoff::g_persp_units);

		if ((int)walkoff::g_drawn.size() > mostRooms)
			mostRooms = (int)walkoff::g_drawn.size();

		if ((int)walkoff::g_drawn.size() < fewestRooms)
			fewestRooms = (int)walkoff::g_drawn.size();

		for (size_t i = 0; i < walkoff::g_drawn.size(); i++)
			tally[(size_t)walkoff::g_drawn[i]]++;

		if (want >= 0)
		{
			if (walkoff::Holds(want))
				wantIn++;

			for (size_t i = 0; i < walkoff::g_verdict.size(); i++)
			{
				const walkoff::Verdict& v = walkoff::g_verdict[i];

				if (v.to != want)
					continue;

				if (!strcmp(v.why, "reached") || !strcmp(v.why, "reach"))
				{
					const double w = v.n[2] - v.n[0];
					const double h = v.n[3] - v.n[1];

					whyCount[0]++;

					if (w > widest) widest = w;
					if (h > tallest) tallest = h;
					if (w < narrowest) narrowest = w;
					if (h < shortest) shortest = h;
				}
				else if (!strcmp(v.why, "side"))     whyCount[1]++;
				else if (!strcmp(v.why, "toonear"))  whyCount[2]++;
				else if (!strcmp(v.why, "empty"))    whyCount[3]++;
				else if (!strcmp(v.why, "covered"))  whyCount[4]++;
				else if (!strcmp(v.why, "offscreen")) whyCount[6]++;
				else                                 whyCount[5]++;

				// And how often the room had no classic window of its own.
				if (!strcmp(v.why, "reach") ||
					!strcmp(v.why, "widened_reach"))
					whyCount[7]++;
			}
		}

		if (verbose)
		{
			if (reach)
				printf("  yaw %6.1f pitch %6.1f  classic=%d reach=%d "
					"(reach alone %d)  rooms:", yaw, pitch, classicRooms,
					(int)walkoff::g_drawn.size(), walkoff::g_reach_only);
			else
				printf("  yaw %6.1f  rooms=%d:", yaw, (int)walkoff::g_drawn.size());

			for (size_t i = 0; i < walkoff::g_drawn.size(); i++)
				printf(" %d", (int)walkoff::g_drawn[i]);

			printf("\n");

			if (reach && (walkoff::g_trim_noart || walkoff::g_trim_budget))
				printf("      trimmed no_art=%d budget=%d\n",
					walkoff::g_trim_noart, walkoff::g_trim_budget);

			// -vis 1: the sources, as VISLOG counts them.
			if (walkoff::g_vis)
				printf("      vis rooms=%d walk=%d box=%d opening=%d portal=%d "
					"dequeues=%ld edge=%d near=%d\n",
					(int)walkoff::g_drawn.size(),
					(int)walkoff::g_drawn.size() - walkoff::g_vis_count[1] -
					walkoff::g_vis_count[2] - walkoff::g_vis_count[3] -
					walkoff::g_vis_count[4] - walkoff::g_vis_count[5],
					walkoff::g_vis_count[1], walkoff::g_vis_count[2],
					walkoff::g_vis_count[3], walkoff::g_dequeues,
					walkoff::g_vis_count[4], walkoff::g_vis_count[5]);

			// -walkfinal: every listed room's answer, as -walkwant gives one.
			if (finals)
				for (size_t i = 0; i < walkoff::g_drawn.size(); i++)
					walkoff::Final(walkoff::g_drawn[i], "      ");

			if (want >= 0)
				walkoff::Why(want);
		}
	}

	printf("-walk: %d view directions, rooms per frame %d..%d\n",
		frames, fewestRooms, mostRooms);

	if (reach && frames)
	{
		printf("-reach: rooms per frame  classic mean %.1f max %d   "
			"reach mean %.1f max %d\n", (double)sumClassic / frames,
			mostClassic, (double)sumReach / frames, mostReach);
		printf("-reach: most rooms one frame owes to the reach alone: %d; "
			"deepest queue %ld (the engine's ring holds 512)\n",
			reachOnlyWorst, queuePeak);
		printf("-reach: worst frame took %ld dequeues; %d frame(s) hit the "
			"cap of %ld\n", worstDequeues, capped, walkoff::kWalkMax);
		printf("-reach: frames over the old budget of 64: %d of %d; over the "
			"new one of 128: %d (draw_rooms holds 256)\n",
			over64, frames, over128);
		printf("-reach: the direction that gains most: yaw %.1f pitch %.1f, "
			"%d rooms -> %d (+%d)\n", bestYaw, bestPitch, bestClassic,
			bestReach, bestGain);
	}

	if (want >= 0)
	{
		printf("-walk: room %d is in the list on %d of %d directions (%.0f%%)\n",
			want, wantIn, frames, frames ? 100.0 * wantIn / frames : 0.0);
		printf("-walk: room %d  reached=%d side=%d toonear=%d empty=%d "
			"covered=%d widened=%d offscreen=%d reach_only=%d\n", want,
			whyCount[0], whyCount[1], whyCount[2], whyCount[3], whyCount[4],
			whyCount[5], whyCount[6], whyCount[7]);

		if (whyCount[0])
			printf("-walk: room %d window when it IS reached: width %.0f..%.0f "
				"of %ld, height %.0f..%.0f of %ld\n", want, narrowest, widest,
				width, shortest, tallest, height);
	}

	/* The rooms the walk reaches, and the rooms it never reaches - with the
	 * distance from the eye and the ROOM_OUTSIDE flag beside each. The
	 * second list is what a "draw the near outside rooms anyway" rule would
	 * be adding, so its length is that rule's cost. */
	int outNear = 0, outAll = 0, neverNear = 0;

	printf("-walk: how often each room is in the list\n");

	for (size_t i = 0; i < tally.size(); i++)
	{
		const walkoff::Room& r = walkoff::g_room[i];
		const long d = walkoff::BoxDistance(r, ex, ey, ez);
		const bool out = (r.flags & 0x8) != 0;

		if (out)
			outAll++;

		if (out && d <= 16384)
			outNear++;

		if (tally[i])
			printf("    room %3d  %4d of %d   dist %6ld  %s\n", (int)i,
				tally[i], frames, d, out ? "OUTSIDE" : "");
		else if (d <= 16384)
			neverNear++;
	}

	printf("-walk: rooms never reached on any direction and within 16 sectors: %d\n", neverNear);
	printf("-walk: ROOM_OUTSIDE rooms: %d of %d in the level, %d of them "
		"within 16 sectors of this eye\n", outAll, (int)tally.size(), outNear);

	for (size_t i = 0; i < tally.size(); i++)
	{
		const walkoff::Room& r = walkoff::g_room[i];
		const long d = walkoff::BoxDistance(r, ex, ey, ez);

		if (!tally[i] && d <= 16384)
			printf("    never  room %3d  dist %6ld  %s\n", (int)i, d,
				(r.flags & 0x8) ? "OUTSIDE" : "");
	}

	return 0;
}

/* -walkdump <log> [frame]: the engine's own whole-list dumps walked again.
 *
 * Every `ROOMLIST frame=N the whole list` block of a -log run carries the
 * eye the walk used, the target it looked at and every listed room with its
 * window and its reach, in the order the walk listed them - the first is
 * the seed. What else the walk depended on the log carries too, and this
 * reads it on the way down to each dump:
 *
 *   the level        the last `HDLOG level=<stem>` line; its .TRC comes out
 *                    of -roombox, which has to hold the files that were
 *                    played
 *   the view         the last ALTERFOV line: the field of view in the
 *                    engine's units, and the height; the width from the last
 *                    `clip first frame ... backbuffer WxH` line
 *   the art          the `HDLOG room= ... hd=` lines of the level build,
 *                    which is HDRoomIsHD per group - the no-art trim's answer
 *   the flips        the `HDLOG flip room=a <-> room=b now group=x / group=y`
 *                    lines: FlipMap swaps the two rooms' records, portals
 *                    and all, and the walk after it runs through the swapped
 *                    ones. HDFlipRoom writes the first ten swaps of a level
 *                    and no more.
 *
 * What no log line carries: which closed doors are shut - a shut door
 * zeroes the normals of the portal it stands in on both sides, and
 * GetRoomBounds follows neither (walkoff::g_doors) - the roll a flyby gives
 * its camera along the spline, and the poison sway that scales the view.
 * The door state and the roll are searched for, per dump, and printed as
 * doors_shut= (the door items) and roll=; the sway is not.
 *
 * Per dump one line - the rooms of both lists, the rooms only one of them
 * has, how many common rooms have the same window, one within a pixel and
 * one further off, and how many the same reach rectangle - and one line per
 * room that differs by more than a pixel, with both windows; a dump that
 * cannot be walked says why. The totals come last. */
static int RunWalkDump(const std::string& folder, const std::string& logPath,
	long onlyFrame, bool verbose)
{
	FILE* f = fopen(logPath.c_str(), "rb");

	if (!f)
	{
		printf("-walkdump: cannot open %s\n", logPath.c_str());
		return 5;
	}

	char stem[64] = "";
	char loadedStem[64] = "";
	short fovUnits = 0;
	long winh = 0, bbw = 0, bbh = 0;
	std::vector<unsigned char> hdGroup;		// per group, as the build said
	std::vector<int> groupOf;			// per room, the group after flips
	std::vector<walkoff::Room> base;		// the level as the file has it
	char line[4096];

	long dumps = 0, walked = 0, listsEqual = 0, doorDumps = 0, rollDumps = 0;
	long winEqual = 0, winNear = 0, winFar = 0, reachEqual = 0, reachOff = 0;
	std::vector<std::string> exceptions;

	walkoff::g_notes = false;

	while (fgets(line, sizeof(line), f))
	{
		const char* body = strchr(line, ']');

		body = body ? body + 1 : line;

		while (*body == ' ')
			body++;

		int a = 0, b = 0, c = 0, d = 0, e = 0;
		const char* at;

		if (!strncmp(body, "HDLOG level=", 12))
		{
			sscanf(body + 12, "%63s", stem);

			if ((at = strstr(body, "backbuffer ")) != 0 &&
				sscanf(at + 11, "%dx%d", &a, &b) == 2)
			{
				bbw = a;
				bbh = b;
			}

			continue;
		}

		if (sscanf(body, "HDLOG room=%d group=%d tris=", &a, &b) == 2 &&
			(at = strstr(body, " hd=")) != 0)
		{
			// A level build starts over at room 0.
			if (a == 0)
			{
				hdGroup.clear();
				groupOf.clear();
			}

			if (b >= 0 && b < 1024)
			{
				if ((int)hdGroup.size() <= b)
					hdGroup.resize((size_t)b + 1, 0);

				hdGroup[(size_t)b] = (unsigned char)(atoi(at + 4) ? 1 : 0);
			}

			continue;
		}

		if (sscanf(body, "HDLOG flip room=%d <-> room=%d now group=%d / group=%d",
			&a, &b, &c, &d) == 4)
		{
			const int most = a > b ? a : b;

			if ((int)groupOf.size() <= most)
			{
				const size_t was = groupOf.size();

				groupOf.resize((size_t)most + 1);

				for (size_t i = was; i < groupOf.size(); i++)
					groupOf[i] = (int)i;
			}

			groupOf[(size_t)a] = c;
			groupOf[(size_t)b] = d;
			continue;
		}

		if (sscanf(body, "ALTERFOV %d (%d deg) winh %d", &a, &b, &c) == 3)
		{
			fovUnits = (short)a;
			winh = c;
			continue;
		}

		unsigned frame = 0;
		int count = 0;

		if (strncmp(body, "ROOMLIST frame=", 15) ||
			!strstr(body, "the whole list") ||
			sscanf(body, "ROOMLIST frame=%u the whole list, %d room(s)", &frame,
				&count) != 2)
			continue;

		long eye[3], tgt[3];

		at = strstr(body, "eye=(");

		if (!at || sscanf(at, "eye=(%ld,%ld,%ld) target=(%ld,%ld,%ld)", &eye[0],
			&eye[1], &eye[2], &tgt[0], &tgt[1], &tgt[2]) != 6)
			continue;

		// The rooms, the next `count` ROOMLIST room lines.
		std::vector<int> rooms;
		std::vector<std::vector<int> > wins, reaches;

		while ((int)rooms.size() < count && fgets(line, sizeof(line), f))
		{
			const char* r = strstr(line, "ROOMLIST   room=");
			int w[4], q[4], rn;

			if (!r || sscanf(r, "ROOMLIST   room=%d window=%d,%d..%d,%d "
				"reach=%d,%d..%d,%d", &rn, &w[0], &w[1], &w[2], &w[3], &q[0],
				&q[1], &q[2], &q[3]) != 9)
				continue;

			rooms.push_back(rn);
			wins.push_back(std::vector<int>(w, w + 4));
			reaches.push_back(std::vector<int>(q, q + 4));
		}

		if (onlyFrame >= 0 && (long)frame != onlyFrame)
			continue;

		dumps++;

		char why[160] = "";
		const long width = bbw ? bbw : winh * 16 / 9;
		const long height = bbh ? bbh : winh;

		if ((int)rooms.size() != count)
			snprintf(why, sizeof(why), "the dump holds %d of its %d room lines",
				(int)rooms.size(), count);
		else if (!stem[0])
			snprintf(why, sizeof(why), "no HDLOG level= line before it");
		else if (!fovUnits || !height)
			snprintf(why, sizeof(why), "no ALTERFOV line before it");

		if (!why[0] && strcmp(stem, loadedStem))
		{
			if (!walkoff::Load(folder, stem))
				snprintf(why, sizeof(why), "no %s.TRC in the -roombox folder", stem);
			else
			{
				base = walkoff::g_room;
				strcpy(loadedStem, stem);
			}
		}

		if (!why[0] && (rooms[0] < 0 || rooms[0] >= (int)base.size()))
			snprintf(why, sizeof(why), "the seed room %d is not in the level",
				rooms[0]);

		if (why[0])
		{
			printf("walkdump frame=%u level=%s not walked: %s\n", frame,
				stem[0] ? stem : "-", why);
			exceptions.push_back(std::string("frame ") + std::to_string(frame) +
				": " + why);
			continue;
		}

		// The art as HDRoomIsHD reads it: through the flips.
		walkoff::g_hd.assign(base.size(), 0);

		bool anyHd = false;

		for (size_t r = 0; r < base.size(); r++)
		{
			const int g = r < groupOf.size() ? groupOf[r] : (int)r;

			if (g >= 0 && (size_t)g < hdGroup.size() && hdGroup[(size_t)g])
			{
				walkoff::g_hd[r] = 1;
				anyHd = true;
			}
		}

		// The reach runs where the level has HD art at all (HDLevelActive).
		walkoff::g_reach = anyHd;
		walkoff::g_use_target = true;
		walkoff::g_fov_units = fovUnits;

		for (int k = 0; k < 3; k++)
			walkoff::g_target[k] = tgt[k];

		/* One walk with the doors in `shut` shut and the camera rolled by
		 * `roll`: the doors' portals zeroed in the rooms of the file, then
		 * the flips, then the walk and the trim. Scored against the dump:
		 * the rooms of the engine's list it misses, the rooms it lists that
		 * the engine does not, the common rooms whose window is more than a
		 * pixel off and those whose reach rectangle is. Two empty windows
		 * are equal whatever their four numbers - a room no frame has
		 * printed keeps the ones the file gave it, 32767,32767..0,0, where
		 * the tool has the wiped ones. */
		std::vector<unsigned char> shut(walkoff::g_doors.size(), 0);
		const std::vector<walkoff::DoorPortals> doors = walkoff::g_doors;

		struct Score
		{
			int missing, extra, windows, reach;
		};

		auto walkWith = [&](const std::vector<unsigned char>& with, short roll)
		{
			std::vector<walkoff::Room> file = base;
			Score s = { 0, 0, 0, 0 };

			walkoff::g_room = file;
			walkoff::ShutDoors(with);
			file = walkoff::g_room;

			for (size_t r = 0; r < base.size(); r++)
			{
				const int g = r < groupOf.size() ? groupOf[r] : (int)r;

				if (g != (int)r && g >= 0 && g < (int)base.size())
					walkoff::g_room[r] = file[(size_t)g];
			}

			walkoff::g_zrot = roll;
			walkoff::One(rooms[0], eye[0], eye[1], eye[2], 0.0, 0.0, width,
				height, 0.0);
			walkoff::g_zrot = 0;

			if (anyHd)
				walkoff::Trim();

			for (size_t i = 0; i < rooms.size(); i++)
			{
				if (!walkoff::Holds(rooms[i]))
				{
					s.missing++;
					continue;
				}

				const walkoff::Room& r = walkoff::g_room[(size_t)rooms[i]];
				const bool toolEmpty = r.left >= r.right || r.top >= r.bottom;
				const bool engineEmpty = wins[i][0] >= wins[i][2] ||
					wins[i][1] >= wins[i][3];

				if (abs(r.box[0] - reaches[i][0]) > 1 ||
					abs(r.box[1] - reaches[i][1]) > 1 ||
					abs(r.box[2] - reaches[i][2]) > 1 ||
					abs(r.box[3] - reaches[i][3]) > 1)
					s.reach++;

				if (toolEmpty && engineEmpty)
					continue;

				if (toolEmpty != engineEmpty ||
					abs(r.left - wins[i][0]) > 1 || abs(r.top - wins[i][1]) > 1 ||
					abs(r.right - wins[i][2]) > 1 || abs(r.bottom - wins[i][3]) > 1)
					s.windows++;
			}

			for (size_t i = 0; i < walkoff::g_drawn.size(); i++)
			{
				bool found = false;

				for (size_t k = 0; k < rooms.size() && !found; k++)
					found = rooms[k] == walkoff::g_drawn[i];

				s.extra += found ? 0 : 1;
			}

			return s;
		};

		// Better: loses no more of the engine's rooms, and fits closer.
		auto better = [](const Score& t, const Score& was)
		{
			return t.missing <= was.missing && (t.extra < was.extra ||
				(t.extra == was.extra && (t.windows < was.windows ||
				(t.windows == was.windows && t.reach < was.reach))));
		};

		Score score = walkWith(shut, 0);

		/* A room the engine does not list, or a window wider than the
		 * engine's: a door the log does not say is shut. Two searches, the
		 * better answer kept. One shuts doors one at a time, and two at a
		 * time when no single one helps (two doors side by side on the two
		 * halves of one opening), while that fits the dump closer and loses
		 * none of the engine's rooms. The other starts with every door shut
		 * and opens them one at a time while that gives back rooms of the
		 * engine's list and adds no surplus. */
		if ((score.extra || score.windows || score.reach) && !doors.empty())
		{
			std::vector<unsigned char> a = shut;
			Score as = score;

			for (bool moved = true; moved && (as.extra || as.windows || as.reach); )
			{
				moved = false;

				for (size_t d = 0; d < doors.size(); d++)
				{
					if (a[d])
						continue;

					std::vector<unsigned char> trial = a;

					trial[d] = 1;

					const Score t = walkWith(trial, 0);

					if (better(t, as))
					{
						a = trial;
						as = t;
						moved = true;
					}
				}

				for (size_t d = 0; !moved && d < doors.size(); d++)
					for (size_t k = d + 1; !moved && k < doors.size(); k++)
					{
						if (a[d] || a[k])
							continue;

						std::vector<unsigned char> trial = a;

						trial[d] = 1;
						trial[k] = 1;

						const Score t = walkWith(trial, 0);

						if (better(t, as))
						{
							a = trial;
							as = t;
							moved = true;
						}
					}
			}

			std::vector<unsigned char> b(doors.size(), 1);
			Score bs = walkWith(b, 0);

			for (bool moved = true; moved && bs.missing; )
			{
				moved = false;

				for (size_t d = 0; d < doors.size() && bs.missing; d++)
				{
					if (!b[d])
						continue;

					std::vector<unsigned char> trial = b;

					trial[d] = 0;

					const Score t = walkWith(trial, 0);

					if (t.missing < bs.missing && t.extra <= bs.extra)
					{
						b = trial;
						bs = t;
						moved = true;
					}
				}
			}

			// A door state that loses a room of the engine's list is no answer.
			shut = !bs.missing && better(bs, as) ? b : a;
		}

		/* Windows still off with the doors settled: a roll the log does not
		 * carry - a flyby rolls its camera along the spline. Every roll in
		 * steps of 16 units (a third of a degree) is tried, then every unit
		 * round the best of them; the one that fits the windows best, and
		 * after them the reach rectangles, is kept when it makes every
		 * window fit - that is the reason. None leaves the dump as it is.
		 * The reach rectangles alone are no sign of a roll: the order the
		 * portals are met in sets them, and a door changes that order. */
		score = walkWith(shut, 0);

		short rollFit = 0;
		bool rollFound = false;

		if (score.windows && !score.missing && !score.extra)
		{
			Score best = score;
			long bestRoll = 0;

			for (int pass = 0; pass < 2; pass++)
			{
				const long from = pass ? bestRoll - 16 : -32768;
				const long to = pass ? bestRoll + 16 : 32767;

				for (long roll = from; roll <= to; roll += pass ? 1 : 16)
				{
					const Score t = walkWith(shut, (short)roll);

					if (!t.missing && !t.extra && (t.windows < best.windows ||
						(t.windows == best.windows && t.reach < best.reach)))
					{
						best = t;
						bestRoll = roll;
					}
				}
			}

			if (!best.windows)
			{
				rollFit = (short)bestRoll;
				rollFound = true;
			}
		}

		score = walkWith(shut, rollFound ? rollFit : 0);
		walkoff::g_zrot = 0;

		std::string shutList;

		for (size_t d = 0; d < shut.size(); d++)
			if (shut[d])
				shutList += (shutList.empty() ? "" : ",") +
					std::to_string(doors[d].item);

		if (!shutList.empty())
			doorDumps++;

		walked++;

		std::string onlyEngine, onlyTool;
		int equal = 0, near1 = 0, offBy = 0, reachSame = 0;
		std::vector<std::string> farLines;

		for (size_t i = 0; i < rooms.size(); i++)
		{
			if (!walkoff::Holds(rooms[i]))
			{
				onlyEngine += (onlyEngine.empty() ? "" : ",") +
					std::to_string(rooms[i]);
				continue;
			}

			const walkoff::Room& r = walkoff::g_room[(size_t)rooms[i]];
			const int t[4] = { r.left, r.top, r.right, r.bottom };
			int worst = 0, reachWorst = 0;

			for (int k = 0; k < 4; k++)
			{
				const int dw = abs(t[k] - wins[i][(size_t)k]);
				const int dr = abs(r.box[k] - reaches[i][(size_t)k]);

				if (dw > worst) worst = dw;
				if (dr > reachWorst) reachWorst = dr;
			}

			// Two empty windows are the same answer, see walkWith.
			if ((t[0] >= t[2] || t[1] >= t[3]) &&
				(wins[i][0] >= wins[i][2] || wins[i][1] >= wins[i][3]))
				worst = 0;

			if (!worst)
				equal++;
			else if (worst <= 1)
				near1++;
			else
			{
				char one[200];

				offBy++;
				snprintf(one, sizeof(one), "  room %d engine=%d,%d..%d,%d tool="
					"%d,%d..%d,%d", rooms[i], wins[i][0], wins[i][1], wins[i][2],
					wins[i][3], t[0], t[1], t[2], t[3]);
				farLines.push_back(one);
			}

			if (!reachWorst)
				reachSame++;
		}

		for (size_t i = 0; i < walkoff::g_drawn.size(); i++)
		{
			bool found = false;

			for (size_t k = 0; k < rooms.size() && !found; k++)
				found = rooms[k] == walkoff::g_drawn[i];

			if (!found)
				onlyTool += (onlyTool.empty() ? "" : ",") +
					std::to_string(walkoff::g_drawn[i]);
		}

		if (onlyEngine.empty() && onlyTool.empty())
			listsEqual++;

		winEqual += equal;
		winNear += near1;
		winFar += offBy;
		reachEqual += reachSame;
		reachOff += equal + near1 + offBy - reachSame;

		printf("walkdump frame=%u level=%s rooms engine=%d tool=%d "
			"only_engine=%s only_tool=%s windows_equal=%d within_1px=%d "
			"differing=%d reach_equal=%d seed=%d fov=%d size=%ldx%ld "
			"trimmed=%d/%d doors_shut=%s roll=%s\n", frame, stem,
			(int)rooms.size(), (int)walkoff::g_drawn.size(),
			onlyEngine.empty() ? "-" : onlyEngine.c_str(),
			onlyTool.empty() ? "-" : onlyTool.c_str(), equal, near1, offBy,
			reachSame, rooms[0], (int)fovUnits, width, height,
			walkoff::g_trim_noart, walkoff::g_trim_budget,
			shutList.empty() ? "-" : shutList.c_str(),
			rollFound ? std::to_string(rollFit).c_str() : "-");

		if (rollFound)
			rollDumps++;

		for (size_t i = 0; i < farLines.size(); i++)
			printf("%s\n", farLines[i].c_str());

		if (verbose)
			for (size_t i = 0; i < walkoff::g_drawn.size(); i++)
				walkoff::Final(walkoff::g_drawn[i], "  ");
	}

	fclose(f);

	printf("walkdump: %ld dump(s) read, %ld walked, room lists equal in %ld; "
		"shut doors found in %ld, a roll in %ld; windows equal %ld, within "
		"1 px %ld, differing %ld; reach equal %ld, differing %ld\n", dumps,
		walked, listsEqual, doorDumps, rollDumps, winEqual, winNear, winFar,
		reachEqual, reachOff);

	for (size_t i = 0; i < exceptions.size(); i++)
		printf("walkdump: not walked, %s\n", exceptions[i].c_str());

	return 0;
}

/* -reachsurvey: the whole level, not one eye.
 *
 * The named eyes below answer "does the reach bring room 99 in"; this
 * answers the other question, the one the safety valve's budget is set
 * from: how many rooms a frame holds under each rule, over the whole level.
 * It puts the eye at the middle of every room's box in turn - the same
 * point -walkcentre uses - walks a coarse fan from it under both rules and
 * keeps the two counts. A room whose box centre is inside a wall is still a
 * fair eye for this: the walk only ever asks where the eye is, and every
 * room of the level gets its turn either way.
 *
 * What it prints is one line per level: the mean and the worst frame under
 * each rule, how often the reach passes 60 and 100 rooms, how deep the queue
 * ever got against the engine's ring of 512 places and its list of 256, and
 * the single room and direction that gains most. The second line is the
 * budget distribution the safety valve's number comes out of. */
static int RunReachSurvey(const std::string& folder, const char* stem,
	const double* yawFan, const double* pitchFan, long width, long height,
	double fov, int step)
{
	if (!walkoff::Load(folder, stem))
		return 5;

	long sumClassic = 0, sumReach = 0, frames = 0;
	int mostClassic = 0, mostReach = 0, over60 = 0, over100 = 0;
	int worstOnly = 0;
	long queuePeak = 0, worstDequeues = 0, worstDequeuesClassic = 0;
	int capped = 0;
	/* How many frames a budget of N rooms would trim, for nine values of N.
	 * The safety valve's number comes out of this column and not out of a
	 * round figure; 96, 112 and 128 are included because draw_rooms holds
	 * 256 and the valve can be opened that far. */
	static const int kBudget[9] = { 32, 40, 48, 56, 64, 80, 96, 112, 128 };
	long overBudget[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	int bestGain = -1, bestRoom = -1, bestClassic = 0, bestReach = 0;
	double bestYaw = 0, bestPitch = 0;
	long reachOnlySum = 0;

	if (step < 1)
		step = 1;

	// Millions of verdicts is what killed the first survey - see kWalkMax.
	walkoff::g_notes = false;

	for (size_t seed = 0; seed < walkoff::g_room.size(); seed += (size_t)step)
	{
		const walkoff::Room& r0 = walkoff::g_room[seed];
		const long ex = r0.x + (long)r0.xs * 512;
		const long ez = r0.z + (long)r0.zs * 512;
		const long ey = (r0.ceiling + r0.floor) / 2;

		for (double pitch = pitchFan[0]; pitch <= pitchFan[1] + 1e-9;
			pitch += pitchFan[2] > 0 ? pitchFan[2] : 1.0)
		for (double yaw = yawFan[0]; yaw <= yawFan[1] + 1e-9; yaw += yawFan[2])
		{
			int classicRooms, now;

			walkoff::g_reach = false;
			walkoff::One((int)seed, ex, ey, ez, yaw, pitch, width, height, fov);
			classicRooms = (int)walkoff::g_drawn.size();

			if (walkoff::g_dequeues > worstDequeuesClassic)
				worstDequeuesClassic = walkoff::g_dequeues;

			walkoff::g_reach = true;
			walkoff::One((int)seed, ex, ey, ez, yaw, pitch, width, height, fov);
			now = (int)walkoff::g_drawn.size();

			frames++;
			sumClassic += classicRooms;
			sumReach += now;
			reachOnlySum += walkoff::g_reach_only;

			if (classicRooms > mostClassic) mostClassic = classicRooms;
			if (now > mostReach) mostReach = now;
			if (now > 60) over60++;
			if (now > 100) over100++;

			for (int b = 0; b < 9; b++)
				if (now > kBudget[b])
					overBudget[b]++;

			if (walkoff::g_reach_only > worstOnly)
				worstOnly = walkoff::g_reach_only;

			if (walkoff::g_queue_peak > queuePeak)
				queuePeak = walkoff::g_queue_peak;

			if (walkoff::g_dequeues > worstDequeues)
				worstDequeues = walkoff::g_dequeues;

			if (walkoff::g_capped)
				capped++;

			if (now - classicRooms > bestGain)
			{
				bestGain = now - classicRooms;
				bestRoom = (int)seed;
				bestYaw = yaw;
				bestPitch = pitch;
				bestClassic = classicRooms;
				bestReach = now;
			}
		}
	}

	if (!frames)
	{
		printf("-reachsurvey: %s has no rooms\n", stem);
		return 5;
	}

	printf("-reachsurvey %-9s rooms=%d frames=%ld  classic mean %.1f max %d  "
		"reach mean %.1f max %d  reach-alone mean %.1f max %d  over60=%ld  "
		"over100=%ld  queue peak %ld  dequeues max %ld (classic %ld) capped=%d "
		"best +%d at room %d yaw %.0f pitch %.0f "
		"(%d -> %d)\n", stem, (int)walkoff::g_room.size(), frames,
		(double)sumClassic / frames, mostClassic, (double)sumReach / frames,
		mostReach, (double)reachOnlySum / frames, worstOnly,
		(long)over60, (long)over100, queuePeak, worstDequeues,
		worstDequeuesClassic, capped, bestGain, bestRoom, bestYaw,
		bestPitch, bestClassic, bestReach);
	printf("-reachbudget %-9s frames over a budget of "
		"32/40/48/56/64/80/96/112/128 rooms: %.1f%% %.1f%% %.1f%% %.1f%% "
		"%.1f%% %.1f%% %.1f%% %.1f%% %.1f%%\n",
		stem, 100.0 * overBudget[0] / frames, 100.0 * overBudget[1] / frames,
		100.0 * overBudget[2] / frames, 100.0 * overBudget[3] / frames,
		100.0 * overBudget[4] / frames, 100.0 * overBudget[5] / frames,
		100.0 * overBudget[6] / frames, 100.0 * overBudget[7] / frames,
		100.0 * overBudget[8] / frames);
	return 0;
}

static int CountIntrusionPairs()
{
	int n = 0;

	for (size_t i = 0; i < g_intrusions_built.size(); i++)
		if (HDRenderIntrusionHits((int)i))
			n++;

	return n;
}

static void ClipFor(int group)
{
	if (g_clip_group >= 0 && group == g_clip_group)
		HDRenderScissor((int)g_clip_box[0], (int)g_clip_box[1],
			(int)g_clip_box[2], (int)g_clip_box[3]);
	else if (g_clip_group >= 0)
		HDRenderScissorOff();
}

static bool DecideGroup(int group, int* rect, bool* full, bool* empty);

/* One group, drawn as the engine would draw it. Ordinarily one call with
 * whatever -window asked for; with -tround naming this group, the two calls
 * DrawRoomWindowed makes for a room on its overlap list; with -roomwin
 * naming it, the calls DrawRoomWindowed makes with that window. */
static bool DrawOneGroup(int group, const D3DMATRIX* world,
	const HDRoomLighting* lighting, unsigned int passes)
{
	if (g_skip_groups.count(group))
		return false;

	/* -hdm: a room the sidecar names WHOLE is drawn classic by the engine,
	 * which means its group never reaches the HD buffers at all
	 * (hd_level.cpp sets g_group_hd[r] = 0 and HDFrameRoom then skips it).
	 * This tool has no classic path to put in its place, so what it draws is
	 * the hole the engine leaves for that path to fill - the seam where the
	 * two paths meet.
	 *
	 * A room named only by cut boxes is not in this list and is drawn, minus
	 * the triangles the cut took out at build time. */
	if (g_hdm && HDMarkersRoomTouched(group))
		return false;

	if (group == g_tround_group)
	{
		HDRenderScissor((int)g_tround_box[0], (int)g_tround_box[1],
			(int)g_tround_box[2], (int)g_tround_box[3]);

		const bool drew = HDRenderRoomMask(group, world, lighting, passes,
			HD_WINDOW_INSIDE);

		HDRenderScissorOff();
		HDRenderRoomMask(group, world, lighting, passes, HD_WINDOW_OVERHANG);
		return drew;
	}

	int rect[4];
	bool full = false, empty = false;

	if (DecideGroup(group, rect, &full, &empty))
	{
		HDRenderScissorOff();

		// -viswin 5: the group's whole art inside its window.
		if (g_vis_option && g_vis_win == 5 && !full)
		{
			bool drew = false;

			if (!empty)
			{
				HDRenderScissor(rect[0], rect[1], rect[2], rect[3]);
				drew = HDRenderRoomMask(group, world, lighting, passes,
					HD_WINDOW_ALL);
				HDRenderScissorOff();
			}

			return drew;
		}

		if (!HDRenderGroupIntrudingTriangles(group) || full)
			return HDRenderRoomMask(group, world, lighting, passes,
				HD_WINDOW_ALL);

		const bool drew = HDRenderRoomMask(group, world, lighting, passes,
			HD_WINDOW_OPEN);

		if (!empty && !(g_vis_option && g_vis_win == 4))
		{
			HDRenderScissor(rect[0], rect[1], rect[2], rect[3]);
			HDRenderRoomMask(group, world, lighting, passes,
				HD_WINDOW_INTRUDING);
			HDRenderScissorOff();
		}

		return drew;
	}

	ClipFor(group);
	return HDRenderRoomMask(group, world, lighting, passes, g_window_mode);
}
static unsigned char g_water_table[22][64][4];

/* -with and -oldorder: the neighbour rooms of one scene.
 *
 * A glow that writes no depth can only be shown to be eaten by a room drawn
 * after it if there is a room drawn after it. -with names a further group
 * and the world offset it stands at - the difference of the two classic room
 * origins, which RoomCheck prints as "x .. z .." per room - and the scene is
 * then drawn the way the engine draws it: HDFrameDraw's order, out of
 * hd_render's own entry points, so this is the engine's order and not a copy
 * of it.
 *
 * -oldorder draws room-major instead: all three passes of the first room,
 * then all three of the second. */
/* As many as draw_rooms holds (game\draw.cpp, kDrawRoomsMax): a picture of
 * what the walk hands the frame is a picture of the whole list, and the
 * reach's lists run to 40 and past 100 rooms. A 257th -with or -roomwin
 * group is refused with a message, never dropped. Each slot is a group
 * number and a translation. */
static const int kWithMax = 256;
static int g_with_group[kWithMax];
static float g_with_offset[kWithMax][3];
static int g_with_count = 0;
static bool g_old_order = false;

/* -roomwin and -decide: a group drawn the way hd_level.cpp's DrawRoomWindowed
 * draws a room, with the window the walk gave it.
 *
 * -roomwin names the window per group exactly as the walk prints it - the
 * engine's inclusive `r->left..r->bottom` - and the tool widens it into the
 * scissor rectangle the way ClipWindow does. The pieces of the group's
 * intruding art (HDPairRange) are then answered per -decide: `whole` draws
 * every piece whole, `scissor` every piece inside the window (the box alone,
 * with no face asked), `frame` gives the engine's own answer (DecidePairs in
 * hd_level.cpp) and `face` holds each claimed piece to its own face alone.
 * A room counts as drawn when it is the seed or a -with group, whether the
 * engine would draw that group in HD or not (HDLOG room= ... hd=). The corner
 * rule is drawroom.cpp's, without the underwater wobble this tool has no
 * camera for. */
static int g_roomwin_group[kWithMax];
static long g_roomwin_box[kWithMax][4];
static int g_roomwin_count = 0;
static int g_decide = 2;			// 0 whole, 1 scissor, 2 frame, 3 face
/* The counters of hd_level.cpp's HDLOG clip line, over the groups -roomwin
 * names that have art, filled once per group by DecideGroup and printed as
 * the `decide: clip` line after the picture - so that a render's counters
 * can be held against the engine's log at the same camera. */
struct DecideCount
{
	int rooms, open, full, scissored, empty;
	int pairs, pairsWhole, pairsScissored;
	int reachRooms, reachPieces, reachWhole, reachHidden;
	int unlinkedPieces, unlinkedDrawn, unlinkedHidden;
};

static DecideCount g_decide_count = { 0 };
static int g_room_probe_seed = -1;		// the group drawn at the origin
static std::vector<HDPairFace> g_faces_built;
static float g_decide_view[16];
static double g_decide_persp = 0.0;
static long g_decide_width = 0, g_decide_height = 0;

static bool FaceSurvives(const HDPairFace& face, const float* offset,
	const long* win)
{
	short common = -1;

	for (int c = 0; c < face.corners && c < 4; c++)
	{
		const float px = face.v[c][0] + offset[0];
		const float py = face.v[c][1] + offset[1];
		const float pz = face.v[c][2] + offset[2];
		const float* view = g_decide_view;
		const float vx = px * view[0] + py * view[4] + pz * view[8] + view[12];
		const float vy = px * view[1] + py * view[5] + pz * view[9] + view[13];
		const float vz = px * view[2] + py * view[6] + pz * view[10] + view[14];
		short clipFlag = 0;

		if (vz < 20.0f)			// f_mznear
			clipFlag = -128;
		else
		{
			const float zv = (float)g_decide_persp / vz;
			const float sx = vx * zv + (float)g_decide_width * 0.5f;
			const float sy = vy * zv + (float)g_decide_height * 0.5f;

			if (sx < (float)win[0])
				clipFlag++;
			else if (sx > (float)win[2])
				clipFlag += 2;

			if (sy < (float)win[1])
				clipFlag += 4;
			else if (sy > (float)win[3])
				clipFlag += 8;
		}

		common &= clipFlag;
	}

	return common == 0;
}

/* The group's -roomwin entry, answered: false when it has none. Sets the
 * scissor rectangle, whether it covers the screen or nothing, and every
 * piece's HDRenderPairWhole, and prints the answers once per group. */
static bool DecideGroup(int group, int* rect, bool* full, bool* empty)
{
	int at = -1;

	for (int i = 0; i < g_roomwin_count; i++)
		if (g_roomwin_group[i] == group)
			at = i;

	if (at < 0)
		return false;

	const long* win = g_roomwin_box[at];

	rect[0] = (int)win[0] - 1;
	rect[1] = (int)win[1] - 1;
	rect[2] = (int)win[2] + 2;
	rect[3] = (int)win[3] + 2;

	if (rect[0] < 0) rect[0] = 0;
	if (rect[1] < 0) rect[1] = 0;
	if (rect[2] > (int)g_decide_width) rect[2] = (int)g_decide_width;
	if (rect[3] > (int)g_decide_height) rect[3] = (int)g_decide_height;

	/* Empty as ClipWindow (hd_level.cpp) calls it: the walk's window has no
	 * width or no height, before any widening - a window one pixel wide
	 * whose two edges are the same pixel is empty there too. */
	*empty = win[2] <= win[0] || win[3] <= win[1];

	if (*empty)
		rect[0] = rect[1] = rect[2] = rect[3] = 0;

	*full = !*empty && rect[0] <= 0 && rect[1] <= 0 &&
		rect[2] >= (int)g_decide_width && rect[3] >= (int)g_decide_height;

	float offset[3] = { 0.0f, 0.0f, 0.0f };

	for (int i = 0; i < g_with_count; i++)
		if (g_with_group[i] == group)
			for (int c = 0; c < 3; c++)
				offset[c] = g_with_offset[i][c];

	static std::set<int> printed;
	const bool say = !printed.count(group);
	int whole = 0, windowed = 0;

	printed.insert(group);

	for (int j = 0; j < HDRenderGroupPairRanges(group); j++)
	{
		HDPairRange piece;

		if (!HDRenderGroupPairRange(group, j, &piece))
			break;

		bool drawn = piece.holder == g_room_probe_seed;

		for (int i = 0; i < g_with_count && !drawn; i++)
			drawn = g_with_group[i] == piece.holder;

		const bool alive = piece.face >= 0 &&
			(size_t)piece.face < g_faces_built.size() &&
			FaceSurvives(g_faces_built[(size_t)piece.face], offset, win);
		// Any face of the same pair that claimed a triangle and survives.
		bool pairAlive = false;

		for (int k = 0; k < HDRenderGroupPairRanges(group) && !pairAlive; k++)
		{
			HDPairRange other;

			if (HDRenderGroupPairRange(group, k, &other) &&
				other.intrusion == piece.intrusion && other.face >= 0 &&
				(size_t)other.face < g_faces_built.size())
				pairAlive = FaceSurvives(g_faces_built[(size_t)other.face],
					offset, win);
		}

		/* frame: DecidePairs' answer - a room that is not drawn is not
		 * asked and its guest's art is drawn whole; otherwise a claimed
		 * piece is whole when any claimed face of its pair survives, and
		 * the triangles no face claims stay in the window. An empty
		 * window asks no face: a piece outside the box one sector smaller
		 * is whole and one inside it is not drawn, whatever its holder.
		 * face: the same with each piece on its own face alone. */
		const bool ask = !*full && !*empty;
		/* A piece with no face outside the smaller box is art the air took
		 * out of the open half: whole under every answer but the air's. */
		const bool open = piece.face < 0 && !piece.inner;
		/* The visibility path (-vis 1) answers as -viswin says; its default,
		 * 2, is DecidePairs' answer there, the frame's. */
		const int decide = !g_vis_option ? g_decide : g_vis_win == 2 ? 2
			: g_vis_win >= 3 ? 1 : 0;
		bool answer = decide == 0 ? true : decide == 1 ? false
			: open ? true
			: *empty ? piece.inner == 0
			: !ask ? false
			: !drawn ? true
			: piece.face < 0 ? false
			: decide == 3 ? alive : pairAlive;

		/* And with -unlinked 1, DecidePairs' last word: a piece whose art
		 * stands in the air of a room its own room has no link with is
		 * drawn inside the window, or not at all when the window is
		 * empty - but for one of a pair with a claimed face that survives
		 * the window, listed holder or not, which keeps the answer above
		 * (whole: a listed holder's pair is whole on that face, an
		 * unlisted holder's on any frame); and so does one no face claims
		 * whose holder is not drawn, on its pair's surviving faces. */
		const bool unlinked = piece.air && g_unlinked && decide >= 2;
		const bool claimed = piece.face >= 0
			? !open && ask && answer && (decide == 3 ? alive : pairAlive)
			: ask && !drawn && pairAlive;

		if (unlinked && !(g_unlinked == 2 && *empty))
			answer = claimed;

		/* -visplane 1: of a pair's claimed faces only a drawn one that is
		 * one wall with the piece's own face keeps the piece whole
		 * (HDVisWallDrawn, DecidePairs). */
		if (g_vis_option && g_vis_plane && unlinked && answer &&
			piece.face >= 0 && (size_t)piece.face < g_faces_built.size())
		{
			HDVisWallFace wall[HD_VIS_WALL_FACES];
			int faceOf[HD_VIS_WALL_FACES];
			int walls = 0;
			int target = -1;

			for (int k = 0; k < HDRenderGroupPairRanges(group) &&
				walls < HD_VIS_WALL_FACES; k++)
			{
				HDPairRange other;
				bool known = false;

				if (!HDRenderGroupPairRange(group, k, &other) ||
					other.intrusion != piece.intrusion || other.face < 0 ||
					(size_t)other.face >= g_faces_built.size())
					continue;

				for (int i = 0; i < walls && !known; i++)
					known = faceOf[i] == other.face;

				if (known)
					continue;

				const HDPairFace& f = g_faces_built[(size_t)other.face];

				if (other.face == piece.face)
					target = walls;

				faceOf[walls] = other.face;
				wall[walls].v = f.v;
				wall[walls].corners = f.corners;
				wall[walls].drawn = FaceSurvives(f, offset, win);
				walls++;
			}

			answer = HDVisWallDrawn(wall, walls, target,
				HD_VIS_PLANE_TOLERANCE);
		}

		/* -viswin 1: the art in unlinked air alone held to the window, on
		 * the claimed faces the room-art rules ask (pairAlive). */
		if (g_vis_option && g_vis_win == 1 && piece.air)
			answer = piece.face >= 0 ? ask && pairAlive
				: ask && !drawn && pairAlive;

		HDRenderPairWhole(group, j, answer ? 1 : 0);

		if (answer)
			whole++;
		else
			windowed++;

		// The engine's clip line counters, once per group.
		if (say && ask && drawn && !open)
		{
			g_decide_count.pairs++;

			if (answer)
				g_decide_count.pairsWhole++;
			else
				g_decide_count.pairsScissored++;
		}

		if (say && unlinked)
		{
			g_decide_count.unlinkedPieces++;

			if (*empty && !answer)
				g_decide_count.unlinkedHidden++;
			else
				g_decide_count.unlinkedDrawn++;
		}

		if (say && *empty && !open)
		{
			g_decide_count.reachPieces++;

			if (answer)
				g_decide_count.reachWhole++;
			else if (piece.inner == 0 && g_decide >= 2)
				g_decide_count.reachHidden++;
		}

		if (say)
			printf("decide: group %3d piece %2d holder %3d face %5d tris %5d "
				"-> %s%s%s%s\n", group, j, piece.holder, piece.face,
				piece.triangles, answer ? "whole" : "window",
				piece.face < 0 ? " (no face)" : alive ? " (face drawn)"
					: " (face dropped)", piece.inner ? " inner" : "",
				piece.air ? " air" : "");

		if (say && g_vis_faces && piece.face >= 0 &&
			(size_t)piece.face < g_faces_built.size())
		{
			const HDPairFace& f = g_faces_built[(size_t)piece.face];

			printf("decide:   face %5d of room %3d, %d corners:", piece.face,
				f.group, f.corners);

			for (int c = 0; c < f.corners && c < 4; c++)
				printf(" %.0f,%.0f,%.0f", f.v[c][0] + offset[0],
					f.v[c][1] + offset[1], f.v[c][2] + offset[2]);

			printf("\n");
		}
	}

	if (say)
		printf("decide: group %3d window %ld,%ld..%ld,%ld%s%s - %d piece(s) "
			"whole, %d inside the window\n", group, win[0], win[1], win[2],
			win[3], *full ? " (the whole screen)" : "",
			*empty ? " (empty)" : "", whole, windowed);

	/* The room as the clip line counts it, and for an empty window the
	 * engine's reachart line per holder - without reach=, which the tool is
	 * not handed, and with the same band_tris out of the same build. */
	if (say && HDRenderGroupTriangles(group, -1) > 0)
	{
		const int count = HDRenderGroupPairRanges(group);

		g_decide_count.rooms++;

		if (!HDRenderGroupIntrudingTriangles(group))
			g_decide_count.open++;
		else if (*empty)
			g_decide_count.empty++;
		else if (*full)
			g_decide_count.full++;
		else
			g_decide_count.scissored++;

		int asked = 0;

		for (int j = 0; j < count; j++)
		{
			HDPairRange piece;

			if (HDRenderGroupPairRange(group, j, &piece) &&
				(piece.face >= 0 || piece.inner))
				asked++;
		}

		if (*empty && asked > 0)
			g_decide_count.reachRooms++;

		for (int j = 0; *empty && j < count; j++)
		{
			HDPairRange piece, other;
			bool seen = false;

			if (!HDRenderGroupPairRange(group, j, &piece))
				break;

			// Art the air took out of the open half has no reach line.
			if (piece.face < 0 && !piece.inner)
				continue;

			for (int k = 0; k < j && !seen; k++)
				seen = HDRenderGroupPairRange(group, k, &other) &&
					other.holder == piece.holder &&
					(other.face >= 0 || other.inner);

			if (seen)
				continue;

			bool drawn = piece.holder == g_room_probe_seed;
			bool anyWhole = false;
			int pieces = 0, tris = 0;

			for (int i = 0; i < g_with_count && !drawn; i++)
				drawn = g_with_group[i] == piece.holder;

			for (int k = j; k < count; k++)
				if (HDRenderGroupPairRange(group, k, &other) &&
					other.holder == piece.holder &&
					(other.face >= 0 || other.inner))
				{
					pieces++;
					tris += other.triangles;

					if (other.inner == 0 &&
						!(other.air && g_unlinked == 1))
						anyWhole = true;
				}

			printf("decide: reachart room=%d holder=%d listed=%d pieces=%d "
				"tris=%d answer=%s band_tris=%d\n", group, piece.holder,
				drawn ? 1 : 0, pieces, tris,
				g_decide < 2 ? (g_decide ? "hidden" : "whole")
					: anyWhole ? "whole" : "hidden",
				HDRenderIntrusionBand(piece.intrusion));
		}
	}

	return true;
}

/* The bulbs go in room local and come out in view space, because that is the
 * space HDFogBulb is in - aBuildFogBulbList (drawroom.cpp) transforms every
 * active bulb by the camera matrix once per frame, and the shader measures
 * the eye ray against it there. Handing a raw room position to the struct
 * would put the bulb somewhere behind the camera. */
static void FillLighting(HDFrameLighting& frame, HDRoomLighting& room,
	float centerX, float centerY, const float* view)
{
	static HDFogBulb viewBulbs[HD_MAX_BULBS];

	for (int i = 0; i < g_bulb_count; i++)
	{
		const HDFogBulb& in = g_bulbs[i];
		HDFogBulb& out = viewBulbs[i];

		out = in;

		if (view)
		{
			out.x = in.x * view[0] + in.y * view[4] + in.z * view[8] + view[12];
			out.y = in.x * view[1] + in.y * view[5] + in.z * view[9] + view[13];
			out.z = in.x * view[2] + in.y * view[6] + in.z * view[10] + view[14];
		}
	}

	memset(&frame, 0, sizeof(frame));
	memset(&room, 0, sizeof(room));

	frame.centerX = centerX;
	frame.centerY = centerY;
	frame.wibble = (float)g_wibble;
	frame.cameraUnderwater = g_underwater ? 1 : 0;
	frame.distanceFogStart = (float)g_fog_start;
	frame.wibbleTable = 0;		// filled by the caller
	frame.bulbs = viewBulbs;
	frame.bulbCount = g_bulb_count;
	frame.clock = (float)g_time;
	frame.motion = g_motion ? 1 : 0;
	frame.reflect = g_reflect ? 1 : 0;
	/* The books turn on the drawn state's time. A picture is one moment
	 * with no tick behind it, so that time is -time itself - the engine's
	 * own at fps_mode 0 or on a frame drawn live. */
	frame.bookClock = g_time;

	if (g_bulb_count && view)
		printf("  bulb 1 in view space: %.0f %.0f %.0f (radius %.0f, density %.4f)\n",
			viewBulbs[0].x, viewBulbs[0].y, viewBulbs[0].z,
			viewBulbs[0].rad, viewBulbs[0].density);

	room.originX = room.originY = room.originZ = 0;	// the tool's world IS room local
	room.surfaceY = -1.0e9f;
	room.underwater = g_underwater ? 1 : 0;
	room.choppy = 1;
	room.surfaceGate = 0;
	room.redAlert = 0;
	room.sel1[3] = (g_shimmer || g_water) ? 1.0f : 0.0f;

	/* -plane and -roomplane stand in for a real water room. The engine sets
	 * three things together for one: the room's surface height, which is the
	 * plane's own y, and the selector that says which mark drives the
	 * shimmer - FillRoomLighting's sel1[1] with its height gate for a plain
	 * water room, sel1[2] with none for a ROOM_REFLECT + ROOM_REFLECT_CEILING
	 * one. Without the height the shader's atSurface is never true and the
	 * swell cannot run at all, which is a picture the engine never draws. */
	if (g_plane)
	{
		room.surfaceY = (float)g_plane_marks[0].y;

		if (g_plane_marks[0].bits & 4)
			room.sel1[2] = 1.0f;
		else
		{
			room.sel1[1] = 1.0f;
			room.surfaceGate = 1;
		}
	}
	room.waterTable = g_water_table[g_mesh_effect];
	room.lights = g_lights;
	room.lightCount = g_light_count;
}

/* The parity proof.
 *
 * Two implementations of one room vertex colour, on the processor:
 * hd_light.cpp's transcription of drawroom.cpp and its transcription of
 * hd_room.hlsl. They are fed the same vertex, and the bridge between the two
 * scales is prelight = (colour1 + colour2) / 2 - the remaster's baked light
 * divided back into the classic path's 0..255, which is exactly the inverse
 * of the shader's light * 127.5.
 *
 * So this is not a test of whether the remaster's art matches Core's
 * lighting; it cannot be, they are different art. It is a test of whether
 * every contribution - the dynamic lights, the water table, the distance
 * fog, the 128 split, the fog bulbs - lands in the same place with the same
 * weight. If it does, then switching a room to HD changes the picture by the
 * baked light and by nothing else.
 */
static void ParityProof(int room, const float* view, int wanted)
{
	HDRenderSample samples[HD_RENDER_SAMPLES];
	HDParityVertex verts[HD_RENDER_SAMPLES];
	HDParityScene scene;
	HDFrameLighting frame;
	HDRoomLighting lighting;
	double worstF[3], worstFinal[3];

	const int have = HDRenderGroupSampleData(room, samples, HD_RENDER_SAMPLES);

	printf("\n-- the parity proof -----------------------------------------\n");

	if (!have)
	{
		printf("group %d kept no samples - nothing to compare.\n", room);
		return;
	}

	FillLighting(frame, lighting, 1.0f, 1.0f, view);

	memset(&scene, 0, sizeof(scene));
	memcpy(scene.view, view, sizeof(scene.view));
	scene.originX = lighting.originX;
	scene.originY = lighting.originY;
	scene.originZ = lighting.originZ;
	scene.surfaceY = lighting.surfaceY;
	scene.underwater = lighting.underwater;
	scene.choppy = lighting.choppy;
	scene.surfaceGate = lighting.surfaceGate;
	scene.redAlert = lighting.redAlert;
	scene.sel1[0] = lighting.sel1[0];
	scene.sel1[1] = lighting.sel1[1];
	scene.sel1[2] = lighting.sel1[2];
	scene.sel1[3] = lighting.sel1[3];
	scene.waterTable = lighting.waterTable;
	scene.wibble = frame.wibble;
	scene.distanceFogStart = frame.distanceFogStart;
	scene.lights = g_lights;
	scene.lightCount = g_light_count;
	scene.bulbs = frame.bulbs;
	scene.bulbCount = frame.bulbCount;

	int count = have < wanted ? have : wanted;

	for (int i = 0; i < count; i++)
	{
		const HDRenderSample& in = samples[i];

		verts[i].x = (float)in.x;
		verts[i].y = (float)in.y;
		verts[i].z = (float)in.z;
		verts[i].nx = ((float)in.nx - 127.0f) * (1.0f / 128.0f);
		verts[i].ny = ((float)in.ny - 127.0f) * (1.0f / 128.0f);
		verts[i].nz = ((float)in.nz - 127.0f) * (1.0f / 128.0f);

		const unsigned long r = ((unsigned long)in.c1[0] + in.c2[0]) / 2;
		const unsigned long g = ((unsigned long)in.c1[1] + in.c2[1]) / 2;
		const unsigned long b = ((unsigned long)in.c1[2] + in.c2[2]) / 2;

		verts[i].prelight = (r << 16) | (g << 8) | b;
		verts[i].bits = in.bits;
	}

	{
		/* How bright the remaster bakes. The classic overbright split only
		 * changes anything above a light of 1, so this says whether it ever
		 * bites in real art. */
		double sum = 0, top = 0;
		int over = 0;

		for (int i = 0; i < count; i++)
			for (int k = 0; k < 3; k++)
			{
				const double L = ((double)samples[i].c1[k] + samples[i].c2[k]) / 255.0;

				sum += L;
				if (L > top) top = L;
				if (L > 1.0) over++;
			}

		printf("baked light over %d vertices: mean %.3f, max %.3f, %d of %d "
			"channels above 1.0\n", count, sum / (count * 3), top, over, count * 3);
	}

	HDLightParity(&scene, verts, count, worstF, worstFinal);

	printf("%d vertices of group %d, lights %d, bulbs %d, fog start %.0f, "
		"underwater %d, shimmer %d, wibble %d\n",
		count, room, g_light_count, g_bulb_count, g_fog_start,
		g_underwater ? 1 : 0, g_shimmer ? 1 : 0, (int)g_wibble);

	printf("worst |classic - shader| of the CONTRIBUTION, in the classic\n"
		"path's own 0..255, before its overbright split - this is the parity:\n");
	printf("  R %.3f   G %.3f   B %.3f\n", worstF[0], worstF[1], worstF[2]);
	printf("and of the FINAL brightness on a white texel, where the split is\n"
		"the deliberate deviation (see the long note in hd_room.hlsl):\n");
	printf("  R %.1f   G %.1f   B %.1f  (of 255)\n",
		worstFinal[0] * 255.0, worstFinal[1] * 255.0, worstFinal[2] * 255.0);

	double worst = 0;

	for (int k = 0; k < 3; k++)
		if (worstF[k] > worst)
			worst = worstF[k];

	printf("verdict: %s (%.3f of 255, budget 2)\n",
		worst <= 2.0 ? "PASS - every contribution lands with the same weight"
			: "FAIL - a contribution drifted", worst);
}

/* ------------------------------------------------------ texture budget -- */

/* What the DXT5 switch costs, as a number.
 *
 * One page is decoded from BC7 to A8R8G8B8 - the picture the lossless default
 * puts on the card - then compressed with the engine's own encoder and
 * decoded again with its inverse, which follows the block layout the hardware
 * decodes. The peak signal to noise ratio between the two is what the switch
 * costs, and it is measured over the whole page, not a corner of it.
 */
static bool ReadWholeFile(const char* path, unsigned char** out, unsigned long* size)
{
	FILE* file = fopen(path, "rb");

	*out = 0;
	*size = 0;

	if (!file)
		return false;

	fseek(file, 0, SEEK_END);
	const long length = ftell(file);
	fseek(file, 0, SEEK_SET);

	if (length <= 0)
	{
		fclose(file);
		return false;
	}

	*out = (unsigned char*)malloc((size_t)length);

	if (!*out)
	{
		fclose(file);
		return false;
	}

	const size_t got = fread(*out, 1, (size_t)length, file);

	fclose(file);
	*size = (unsigned long)length;
	return got == (size_t)length;
}

static void PrintPsnr(const char* root, int howMany)
{
	printf("\n-- what DXT5 costs ------------------------------------------\n");

	if (HDPageCacheSlots() <= 0)
	{
		printf("no page is loaded - nothing to measure\n");
		return;
	}

	double sum = 0, worst = 999, best = 0;
	int measured = 0;

	for (int slot = 0; slot < HDPageCacheSlots() && measured < howMany; slot++)
	{
		const int page = HDPageCachePageOfSlot(slot);
		char buffer[300];
		unsigned char* file = 0;
		unsigned long size = 0;

		if (page < 0 || !ReadWholeFile(HDScanPagePath(root, page, buffer, sizeof(buffer)),
			&file, &size))
			continue;

		long w = 0, h = 0;
		const char* why = "";
		unsigned long* pixels = BC7DecodeDDS(file, size, 4096, &w, &h, &why);

		free(file);

		if (!pixels)
			continue;

		unsigned char* blocks = (unsigned char*)malloc(DXT5Size(w, h));
		unsigned long* back = (unsigned long*)malloc((size_t)w * (size_t)h * 4);

		if (blocks && back)
		{
			DXT5Encode(pixels, w, h, blocks);
			DXT5Decode(blocks, w, h, back);

			const double db = DXT5Psnr(pixels, back, w * h);

			if (measured < 4)
				printf("page %-5d %4ldx%-4ld  %7.2f dB   %ld KiB -> %ld KiB\n",
					page, w, h, db, (long)(w * h * 4 / 1024),
					(long)(DXT5Size(w, h) / 1024));

			sum += db;
			measured++;

			if (db < worst) worst = db;
			if (db > best) best = db;
		}

		free(blocks);
		free(back);
		free(pixels);
	}

	if (!measured)
	{
		printf("no page could be decoded - nothing to measure\n");
		return;
	}

	printf("%d page(s) measured: mean %.2f dB, worst %.2f dB, best %.2f dB\n",
		measured, sum / measured, worst, best);
	printf("(mip 0 only; a quarter of the video memory is what is bought with it)\n");
}

/* The words a water square reads of one square's floor data (HDWaterRoom),
 * out of the classic .TRC - the twin of hd_level.cpp's WaterSquareWords,
 * which reads floor_data: the floor's slope entry as Tr5Level::floorSlope
 * finds it (the first TILT, SPLIT1/2 or NOCOLF entry, GetHeight's), the
 * ceiling's as GetMinimumCeiling reads it (the first entry, or the second
 * behind a floor slope, when a ROOF, SPLIT3/4 or NOCOLC entry), and the
 * first TO_SINK command's camera entry, a camera's and a flyby's command
 * taking one word more. */
static void ProbeWaterWords(const Tr5Level& level, int room, int sx, int sz,
	unsigned short* slope, short* sink)
{
	slope[0] = slope[1] = slope[2] = slope[3] = 0;
	*sink = -1;

	const int index = level.sectorIndex(room, sx, sz);
	std::vector<Tr5FloorEntry> chain;

	if (index <= 0 || !level.readChain(index, chain) || chain.empty())
		return;

	auto floorSlope = [](int fn)
	{
		return fn == 2 || fn == 7 || fn == 8 || (fn >= 11 && fn <= 14);
	};

	for (size_t i = 0; i < chain.size(); i++)
		if (floorSlope(chain[i].function) && chain[i].words.size() >= 2)
		{
			slope[0] = chain[i].words[0];
			slope[1] = chain[i].words[1];
			break;
		}

	const Tr5FloorEntry* c = &chain[0];

	if (floorSlope(c->function))
		c = (c->words[0] & 0x8000) || chain.size() < 2 ? 0 : &chain[1];

	if (c && c->words.size() >= 2 && (c->function == 3 || c->function == 9 ||
		c->function == 10 || (c->function >= 15 && c->function <= 18)))
	{
		slope[2] = c->words[0];
		slope[3] = c->words[1];
	}

	for (size_t i = 0; i < chain.size() && *sink < 0; i++)
	{
		if (chain[i].function != 4)
			continue;

		for (size_t k = 2; k < chain[i].words.size(); k++)
		{
			const int type = (chain[i].words[k] >> 10) & 0xF;

			if (type == 2)
			{
				*sink = (short)(chain[i].words[k] & 0x3FF);
				break;
			}

			if (type == 1 || type == 12)
				k++;
		}
	}
}

/* The proof of the water squares' corners: for every square of a dry room
 * over an underwater room, the corners HDRenderWaterCorners makes of the
 * words above against Tr5Level::cornerFloor and cornerCeiling - the dry
 * floor's and, at the same place in the room below, the water's ceiling. */
static void ProbeWaterCorners(const Tr5Level& level,
	const std::vector<HDWaterRoom>& rooms)
{
	long squares = 0, equal = 0;

	for (size_t r = 0; r < rooms.size(); r++)
	{
		const HDWaterRoom& w = rooms[r];

		if ((w.flags & HD_WATER_ROOM_UNDERWATER) || !w.floor || !w.below ||
			!w.slope)
			continue;

		for (int sx = 0; sx < w.sizeX; sx++)
			for (int sz = 0; sz < w.sizeZ; sz++)
			{
				const int at = sx * w.sizeZ + sz;
				const int pit = w.below[at];

				if (pit < 0 || pit >= (int)rooms.size() ||
					w.floor[at] == -127 ||
					!(rooms[(size_t)pit].flags & HD_WATER_ROOM_UNDERWATER) ||
					!rooms[(size_t)pit].slope)
					continue;

				const HDWaterRoom& b = rooms[(size_t)pit];
				const int bx = (w.originX + sx * 1024 - b.originX) >> 10;
				const int bz = (w.originZ + sz * 1024 - b.originZ) >> 10;

				if (bx < 0 || bz < 0 || bx >= b.sizeX || bz >= b.sizeZ)
					continue;

				const int bi = bx * b.sizeZ + bz;
				int f[4], c[4];
				bool same = true;

				HDRenderWaterCorners(w.floor[at], w.slope[at * 4],
					w.slope[at * 4 + 1], 0, f);
				HDRenderWaterCorners(b.ceiling[bi], b.slope[bi * 4 + 2],
					b.slope[bi * 4 + 3], 1, c);

				for (int k = 0; k < 4; k++)
				{
					long y = 0;

					if (!level.cornerFloor((int)r, sx, sz, (k & 1) ? 1024 : 0,
						(k & 2) ? 1024 : 0, y) || y != f[k])
						same = false;

					if (!level.cornerCeiling(pit, bx, bz, (k & 1) ? 1024 : 0,
						(k & 2) ? 1024 : 0, y) || y != c[k])
						same = false;
				}

				squares++;
				equal += same ? 1 : 0;
			}
	}

	/* And the formula over every square of the level, floor and ceiling,
	 * where the words above are the square's own entries - the slopes the
	 * water squares meet are few, the level's are thousands. */
	long all = 0, allEqual = 0, sloped = 0;

	for (size_t r = 0; r < rooms.size(); r++)
	{
		const HDWaterRoom& w = rooms[r];

		if (!w.floor || !w.slope || !w.ceiling)
			continue;

		for (int sx = 0; sx < w.sizeX; sx++)
			for (int sz = 0; sz < w.sizeZ; sz++)
			{
				const int at = sx * w.sizeZ + sz;

				if (w.floor[at] == -127 || w.ceiling[at] == -127)
					continue;

				int f[4], c[4];
				bool same = true;

				HDRenderWaterCorners(w.floor[at], w.slope[at * 4],
					w.slope[at * 4 + 1], 0, f);
				HDRenderWaterCorners(w.ceiling[at], w.slope[at * 4 + 2],
					w.slope[at * 4 + 3], 1, c);

				for (int k = 0; k < 4; k++)
				{
					long y = 0;

					if (!level.cornerFloor((int)r, sx, sz, (k & 1) ? 1024 : 0,
						(k & 2) ? 1024 : 0, y) || y != f[k])
						same = false;

					if (!level.cornerCeiling((int)r, sx, sz, (k & 1) ? 1024 :
						0, (k & 2) ? 1024 : 0, y) || y != c[k])
						same = false;
				}

				all++;
				allEqual += same ? 1 : 0;
				sloped += w.slope[at * 4] || w.slope[at * 4 + 2] ? 1 : 0;
			}
	}

	printf("water corners: %ld of %ld squares agree with Tr5Level, floor and "
		"ceiling, corner for corner; over the whole level %ld of %ld (%ld "
		"with a slope)\n", equal, squares, allEqual, all, sloped);
}

/* The tanks' films out of the classic .TRC - the twin of hd_level.cpp's
 * BuildWaterFilms: every FLOOR_LASERS item (slot 392) in the room of a
 * FISHTANK (slot 413), its box as InitialiseFloorLasers lays it - the
 * item's trigger_flags as width (the ones, squares along z) and height (the
 * tens, along x), from 512 before the item's place. The FISHTANK mesh
 * goes to the renderer first, as hd_level.cpp's TankMeshToRenderer hands
 * it (vertices, quads, a triangle as a quad that repeats its third
 * corner). And for -tank, the tank's place and its mesh's root, as
 * ProbeWaterFalls reads a fall's. */
static void ProbeTankMesh(const Tr5Level& level)
{
	const std::vector<uint8_t>& bytes = level.debugBytes();
	Tr5Level::MoveableInfo info;
	int16_t head[6];
	size_t at = 0;
	std::vector<float> xyz;
	std::vector<int> quads;

	if (level.moveableInfo(413, info) && info.nmeshes > 0 &&
		level.debugMeshHeader(info.meshIndex, head) &&
		level.debugMeshAt(info.meshIndex, at) && head[5] > 0)
	{
		// The mesh: vertices, normals or shades, quads (6 words), triangles
		// (5 words) - the walk ProbeWaterFalls takes to its vertices.
		size_t p = at + 12;
		int16_t w[6];

		for (int v = 0; v < head[5] && p + 6 <= bytes.size(); v++, p += 6)
		{
			std::memcpy(w, &bytes[p], 6);
			xyz.push_back(w[0]);
			xyz.push_back(w[1]);
			xyz.push_back(w[2]);
		}

		int16_t n = 0;

		if (p + 2 <= bytes.size())
			std::memcpy(&n, &bytes[p], 2);

		p += 2 + (n > 0 ? (size_t)n * 6 : (size_t)(-n) * 2);

		for (int pass = 0; pass < 2 && p + 2 <= bytes.size(); pass++)
		{
			const size_t stride = pass ? 10 : 12;

			std::memcpy(&n, &bytes[p], 2);
			p += 2;

			for (int q = 0; q < n && p + stride <= bytes.size(); q++,
				p += stride)
			{
				std::memcpy(w, &bytes[p], 8);

				for (int k = 0; k < 4; k++)
					quads.push_back(w[pass && k == 3 ? 2 : k]);
			}
		}
	}

	HDRenderWaterTankMesh(xyz.empty() ? 0 : &xyz[0], (int)xyz.size() / 3,
		quads.empty() ? 0 : &quads[0], (int)quads.size() / 4);
	if (g_tank_item >= 0)
		printf("-tank: FISHTANK mesh %d vertices %d quads\n",
			(int)xyz.size() / 3, (int)quads.size() / 4);
}

static void ProbeWaterFilms(const Tr5Level& level)
{
	std::vector<HDWaterFilm> films;
	std::vector<HDWaterWetTank> tanks;

	ProbeTankMesh(level);
	g_tank_film = -1;
	g_tank_found = false;

	for (int t = 0; t < level.itemCount(); t++)
	{
		Tr5Item tank;

		if (!level.getItem(t, tank) || tank.objectNumber != 413)
			continue;

		// Every tank for its wet floor, as hd_level.cpp hands them in.
		const HDWaterWetTank wet = { t, tank.roomNumber, tank.x, tank.y,
			tank.z, tank.yRot };

		tanks.push_back(wet);

		if (g_tank_item >= 0)
			printf("-tank: FISHTANK item %d room %d at %d %d %d turn %d\n", t,
				tank.roomNumber, tank.x, tank.y, tank.z, tank.yRot);

		if (t == g_tank_item)
		{
			Tr5Level::MoveableInfo info;

			g_tank_place = tank;
			g_tank_found = true;

			if (level.moveableInfo(413, info))
			{
				const long word = (long)(info.frameOffset / 2);

				for (int k = 0; k < 3; k++)
					g_tank_root[k] = level.debugReadFrameShort(word + 6 + k);
			}
		}

		for (int l = 0; l < level.itemCount(); l++)
		{
			Tr5Item laser;

			if (!level.getItem(l, laser) || laser.objectNumber != 392 ||
				laser.roomNumber != tank.roomNumber)
				continue;

			const int width = laser.triggerFlags % 10;
			const int height = laser.triggerFlags / 10;
			HDWaterFilm f;

			f.room = laser.roomNumber;
			f.x0 = (laser.x - 512) & ~1023;
			f.x1 = f.x0 + height * 1024;
			f.z0 = (laser.z - 512) & ~1023;
			f.z1 = f.z0 + width * 1024;
			f.y = laser.y;
			f.tank = t;
			f.laser = l;

			if (t == g_tank_item)
				g_tank_film = (int)films.size();

			printf("-tank: film %d room %d x %d..%d z %d..%d y %d tank %d "
				"laser %d (trigger_flags %d)\n", (int)films.size(), f.room,
				f.x0, f.x1, f.z0, f.z1, f.y, t, l, laser.triggerFlags);
			films.push_back(f);
		}

	}

	HDRenderWaterFilms(films.empty() ? 0 : &films[0], (int)films.size());
	HDRenderWaterWetTanks(tanks.empty() ? 0 : &tanks[0], (int)tanks.size());
}

// The cards' pages into the cache, with the sheets, as hd_level.cpp adds them.
static void ProbeCardPages(const char* root, bool managed, int threads)
{
	if (g_hdwater != 1 || !HDRenderWaterSheetsBuilt())
		return;

	int pages[4];
	const int n = HDRenderWaterCardPages(pages, 4);
	HDPageCacheStats stats;
	const char* why = "";

	if (!HDPageCacheAppend(g_device, root, pages, n, managed, threads, &stats,
		&why))
		printf("-hdwater: the cards' pages refused - %s\n", why);
}

/* ControlFishtank's table, game\tower.cpp: the points along the tank's
 * front the spray comes from, x and z pairs about joint 0. */
static const short kSplashOffsets[18] = { 1072, 48, 1072, 48, 650, 280, 200,
	320, -300, 320, -800, 320, -1200, 320, -1650, 280, -2112, 48 };

/* The splash and the tank of -splash and -tank, handed to the cards at the
 * age the picture asks for. The tank: ControlFishtank run tick by tick to
 * the picture's time (hd_splash_math.h, HDTankFlagsAfter), each tick's
 * points - 5 on an even tick, 4 on an odd one, the inner seven moved along
 * their segment by this file's hash where the game rolls GetRandomControl
 * - through the tank's place, turn and root (joint 0 as GetJointAbsPosition
 * gives it with the joint's own turn left at 0), the strength
 * TriggerFishtankSpray takes; the sparks of the last 18 ticks are handed
 * in (a spark's card lives 17 at most).
 * After the tank the lasers' count runs down from 64 a tick, and the film
 * takes the strength HDFilmStrength gives - what hd_level.cpp's
 * FeedWaterFilms reads off the items in the game. A tank without floor
 * lasers in its room has no film (-1 on the line); its spray is drawn.
 * While the tank runs, its water is handed in at the picture's age. */
static void ProbeCardsFeed()
{
	if (g_hdwater != 1)
		return;

	if (g_splash_set)
	{
		const int fs = g_splash_fall;
		HDSplashSetup s;

		s.x = g_splash_at[0];
		s.y = g_splash_at[1];
		s.z = g_splash_at[2];

		if (g_splash_kind == 1)
		{
			s.innerRad = 16; s.innerSize = 12; s.innerRadVel = 160;
			s.innerYVel = -72 * fs;
			s.middleRad = 24; s.middleSize = 24; s.middleRadVel = 224;
			s.middleYVel = -36 * fs;
			s.outerRad = 32; s.outerSize = 32; s.outerRadVel = 272;
		}
		else if (g_splash_kind == 2)
		{
			const int wh = 2048 - fs;

			s.innerRadVel = 160; s.middleRadVel = 224; s.outerRadVel = 272;
			s.innerRad = (wh >> 6) + 16; s.innerSize = (wh >> 6) + 12;
			s.innerYVel = (-512 - wh) << 3;
			s.middleRad = (wh >> 6) + 24; s.middleSize = (wh >> 6) + 24;
			s.middleYVel = (-768 - wh) << 2;
			s.outerRad = (wh >> 6) + 32; s.outerSize = (wh >> 6) + 32;
		}
		else
		{
			s.innerRad = 32; s.innerSize = 8; s.innerRadVel = 320;
			s.innerYVel = -40 * fs;
			s.middleRad = 48; s.middleSize = 32; s.middleRadVel = 480;
			s.middleYVel = -20 * fs;
			s.outerRad = 32; s.outerSize = 128; s.outerRadVel = 544;
		}

		const HDSplashNow now = HDSplashAt(s, g_splash_at[3]);

		printf("-splash: kind %d fall %d at %.0f %.0f %.0f, %.2f s: %s, "
			"rings %.0f/%.0f, column %.0f/%.0f, life %.2f\n", g_splash_kind,
			fs, s.x, s.y, s.z, g_splash_at[3],
			HDRenderWaterSplash(&s, g_splash_at[3]) ? "taken" :
			"NOT taken (no sheet under it or no cards)", now.middleR,
			now.outerR, now.innerH, now.middleH, now.life);
	}

	if (g_tank_item < 0)
		return;

	if (!g_tank_found)
	{
		printf("-tank: item %d is no FISHTANK - nothing\n", g_tank_item);
		return;
	}

	const float c = enginecam::PhdCos(g_tank_place.yRot) * (1.0f / 16384.0f);
	const float sn = enginecam::PhdSin(g_tank_place.yRot) * (1.0f / 16384.0f);
	const int now = g_tank_time < 0.0f ? -1 : (int)(g_tank_time * 30.0f);
	int running = 0;
	const int flags = HDTankFlagsAfter(now < 0 ? 0 : now, &running);
	int sprays = 0;
	float lowest = -1.0e9f;

	for (int k = now - 18; now >= 0 && k <= now; k++)
	{
		int on = 0;
		const int f1 = HDTankFlagsAfter(k < 0 ? 0 : k, &on);

		if (k < 0 || !on)
			continue;

		for (int i = k & 1; i < 9; i += 2)
		{
			int x = kSplashOffsets[2 * i];
			int z = kSplashOffsets[2 * i + 1];

			if (i && i != 8)
			{
				const unsigned int h = HDCardHash((unsigned int)k,
					(unsigned int)i);
				const int dx = abs(kSplashOffsets[2 * i + 2] - x);
				const int dz = kSplashOffsets[2 * i + 3] - z;

				x -= dx ? (int)(h % (unsigned int)dx) : 0;

				if (dz)
					z += (int)((h >> 16) % (unsigned int)abs(dz)) *
						(dz < 0 ? -1 : 1);
			}

			const float lx = (float)x + g_tank_root[0];
			const float lz = (float)z + g_tank_root[2];
			const float wx = (float)g_tank_place.x + lx * c + lz * sn;
			const float wz = (float)g_tank_place.z + lz * c - lx * sn;
			const float wy = (float)g_tank_place.y + g_tank_root[1];
			int strength = ((f1 - 800) >> 3) >> 1;

			strength = strength > 64 ? 64 : strength;
			lowest = wy > lowest ? wy : lowest;

			if (!(g_tank_leave & 1) && HDRenderWaterSpray(wx, wy, wz,
				strength, g_tank_time - (float)k / 30.0f))
				sprays++;
		}
	}

	// The lasers after the tank: 64 when it stops, one less a tick.
	const int stop = 105;
	const int state = now < 0 ? 0 : running ? 1 : 2;
	const int count = state == 2 ? 64 - (now - stop) : 64;
	const float wet = HDFilmStrength(state, flags, count > 0 ? 1 : 0, count);

	if (g_tank_film >= 0)
		HDRenderWaterFilmWet(g_tank_film, wet);

	// Its wet floor at the picture's age, or held at -tankwet's strength.
	HDRenderWaterWet(g_tank_item, state, flags, g_tank_time);
	HDRenderWaterWetFixed(g_tank_wet);

	// The tank's water while ControlFishtank runs it, as DrawScaledSpike
	// hands it in the game (hd_level.cpp, HDWaterTank).
	const bool water = state == 1 && !(g_tank_leave & 2) &&
		HDRenderWaterTank(g_tank_item, (float)g_tank_place.x,
		(float)g_tank_place.y, (float)g_tank_place.z, g_tank_place.yRot,
		flags, g_tank_time);

	printf("-tank: the tank's water %s\n", water ? "drawn" : "not drawn");

	printf("-tank: item %d at %.2f s: %s, item_flags[1] %d, lasers %d, film "
		"%d wet %.2f, %d sparks handed in, spray at y %.0f (root %.0f %.0f "
		"%.0f)\n", g_tank_item, g_tank_time, state == 0 ? "before" :
		state == 1 ? "spraying" : "done", flags, count, g_tank_film, wet,
		sprays, lowest, g_tank_root[0], g_tank_root[1], g_tank_root[2]);
}

/* The cards drawn where HDFrameDrawGlow draws them, the picture's world
 * shifted by the seed room's origin (the tool's world is the seed room's),
 * timed on the GPU with a timestamp pair; the line is -hdwater's, so the
 * timing scripts add it to the water's other phases. */
static void ProbeCardsDraw()
{
	if (g_hdwater != 1 || (!g_splash_set && g_tank_item < 0))
		return;

	float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
		-g_seed_origin[0], 0, -g_seed_origin[1], 1 };
	IDirect3DQuery9* q[3] = { 0, 0, 0 };
	IDirect3DQuery9* freq = 0;

	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &q[0]);
	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1]);
	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2]);
	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &freq);

	const bool timed = q[0] && q[1] && q[2] && freq;

	if (timed)
	{
		q[0]->Issue(D3DISSUE_BEGIN);
		q[1]->Issue(D3DISSUE_END);
	}

	HDRenderWaterCardsDraw(m);

	if (timed)
	{
		q[2]->Issue(D3DISSUE_END);
		q[0]->Issue(D3DISSUE_END);
		freq->Issue(D3DISSUE_END);
	}

	HDRenderSplashStats st;

	HDRenderSplashStatsNow(&st);

	double ms = -1.0;

	if (timed)
	{
		UINT64 a = 0, b = 0, f = 0;
		BOOL disjoint = TRUE;

		while (q[0]->GetData(&disjoint, sizeof(disjoint), D3DGETDATA_FLUSH) ==
			S_FALSE)
			;

		while (q[1]->GetData(&a, sizeof(a), D3DGETDATA_FLUSH) == S_FALSE)
			;

		while (q[2]->GetData(&b, sizeof(b), D3DGETDATA_FLUSH) == S_FALSE)
			;

		while (freq->GetData(&f, sizeof(f), D3DGETDATA_FLUSH) == S_FALSE)
			;

		if (!disjoint && f)
			ms = (double)(b - a) * 1000.0 / (double)f;
	}

	for (int i = 0; i < 3; i++)
		if (q[i])
			q[i]->Release();

	if (freq)
		freq->Release();

	printf("SPLASHLOG level=probe splashes=%d cards=%d rings=%d tank=%d:%.2f "
		"tanks=%d floor=%d:%.2f\n", st.splashes, st.cards, st.rings, st.tank,
		st.wet, st.tanks, st.floor, st.floorWet);
	printf("-hdwater: cards %d cards in 3 draws at most, GPU %.4f ms\n",
		st.cards, ms);
}

/* -tankslopes: every square of the classic level within 4,096 units of a
 * kept square of any wet floor whose floor slopes (a TILT, SPLIT1/2 or
 * NOCOLF entry), one line each: its room, corner, the four corners' y
 * (HDRenderWaterCorners: x0z0, x1z0, x0z1, x1z1) and the entry's function.
 * What the wet floor's rule then makes of them is -tankwetmap's. */
static bool g_tank_slopes = false;

static void ProbeWetSlopes(const std::vector<HDWaterRoom>& rooms)
{
	if (!g_tank_slopes)
		return;

	int x0 = 0x7FFFFFFF, z0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF, z1 = -0x7FFFFFFF;
	HDWaterWetSquare s;

	for (int f = 0; f < 8; f++)
		for (int at = 0; HDRenderWaterWetSquare(f, at, &s); at++)
			if (s.kept)
			{
				x0 = s.x < x0 ? s.x : x0;
				z0 = s.z < z0 ? s.z : z0;
				x1 = s.x + 1024 > x1 ? s.x + 1024 : x1;
				z1 = s.z + 1024 > z1 ? s.z + 1024 : z1;
			}

	for (size_t r = 0; r < rooms.size(); r++)
	{
		const HDWaterRoom& w = rooms[r];

		for (int sx = 0; w.floor && w.slope && sx < w.sizeX; sx++)
			for (int sz = 0; sz < w.sizeZ; sz++)
			{
				const int at = sx * w.sizeZ + sz;
				const int fn = w.slope[at * 4] & 0x1F;
				const int wx = w.originX + sx * 1024;
				const int wz = w.originZ + sz * 1024;

				if (w.floor[at] == -127 || !(fn == 2 || fn == 7 || fn == 8 ||
					(fn >= 11 && fn <= 14)) || wx < x0 - 4096 ||
					wx >= x1 + 4096 || wz < z0 - 4096 || wz >= z1 + 4096)
					continue;

				int c[4];

				HDRenderWaterCorners(w.floor[at], w.slope[at * 4],
					w.slope[at * 4 + 1], 0, c);
				printf("wetslope room=%d x=%d z=%d corners=%d,%d,%d,%d "
					"fn=%d\n", (int)r, wx, wz, c[0], c[1], c[2], c[3], fn);
			}
	}
}

/* -tankart: the .TRG's art over and under every kept square of floor
 * `floor`, group by group, one line each: the triangles whose centroid lies
 * in the square's column within 16 units of its floor (flat ones, and the
 * pages and passes they are on), under it and over it; of those under it,
 * the ones whose centroid mirrored in the floor lands within 32 units of
 * the centroid of a triangle over it (any group) - the copy of a room
 * under its floor - and how deep the deepest lies. */
static bool g_tank_art = false;

static void ProbeWetArt(int floor, const char* root, const char* stem)
{
	if (!g_tank_art)
		return;

	static trf::remaster::Trg trg;
	static bool read = false;

	if (!read)
	{
		std::string error;

		read = true;

		if (!trf::remaster::read_trg(std::string(root) + "\\DATA\\" +
			stem + ".TRG", trg, error))
			printf("wetart: no .TRG (%s)\n", error.c_str());
	}

	struct Tri
	{
		int group, pass, page;
		float x, y, z, ny;
	};

	std::map<long long, std::vector<Tri> > column;
	const auto key = [](int sx, int sz)
	{
		return ((long long)sx << 32) ^ (long long)(unsigned int)sz;
	};
	const auto cell = [](float x, float y, float z)
	{
		return ((long long)((int)floorf(x / 32.0f) & 0xFFFFF) << 40) |
			((long long)((int)floorf(y / 32.0f) & 0xFFFFF) << 20) |
			(long long)((int)floorf(z / 32.0f) & 0xFFFFF);
	};
	HDWaterWetSquare s;
	std::set<long long> wanted;

	for (int at = 0; HDRenderWaterWetSquare(floor, at, &s); at++)
		if (s.kept)
			wanted.insert(key(s.x >> 10, s.z >> 10));

	for (size_t g = 0; g < trg.groups.size() && g < g_boxes_built.size(); g++)
		for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
			for (int pass = 0; pass < 3; pass++)
				for (unsigned int i = 0; i + 2 < part.pass[pass].count; i += 3)
				{
					const unsigned int first = part.pass[pass].start + i;
					float p[3][3];
					int page = 0;

					for (int v = 0; v < 3; v++)
					{
						const trf::remaster::TrgVertex& tv =
							trg.vertices[trg.indices[first + v]];

						p[v][0] = (float)tv.x + (float)g_boxes_built[g].originX;
						p[v][1] = (float)tv.y;
						p[v][2] = (float)tv.z + (float)g_boxes_built[g].originZ;
						page = tv.layer && tv.layer <= trg.textures.size() ?
							trg.textures[tv.layer - 1u] : -1;
					}

					Tri t = { (int)g, pass, page,
						(p[0][0] + p[1][0] + p[2][0]) / 3.0f,
						(p[0][1] + p[1][1] + p[2][1]) / 3.0f,
						(p[0][2] + p[1][2] + p[2][2]) / 3.0f, 0.0f };
					const float ax = p[1][0] - p[0][0], ay = p[1][1] - p[0][1];
					const float az = p[1][2] - p[0][2], bx = p[2][0] - p[0][0];
					const float by = p[2][1] - p[0][1], bz = p[2][2] - p[0][2];
					const float nx = ay * bz - az * by, ny = az * bx - ax * bz;
					const float nz = ax * by - ay * bx;
					const float l = sqrtf(nx * nx + ny * ny + nz * nz);

					t.ny = l > 0.0f ? fabsf(ny) / l : 0.0f;

					const long long k = key((int)floorf(t.x / 1024.0f),
						(int)floorf(t.z / 1024.0f));

					if (wanted.count(k))
						column[k].push_back(t);
				}

	for (int at = 0; HDRenderWaterWetSquare(floor, at, &s); at++)
	{
		if (!s.kept)
			continue;

		const std::vector<Tri>& all = column[key(s.x >> 10, s.z >> 10)];
		std::set<long long> over;
		std::map<int, int> groups;

		for (const Tri& t : all)
		{
			groups[t.group]++;

			if (t.y < (float)s.y - 16.0f)
				over.insert(cell(t.x, t.y, t.z));
		}

		for (const auto& gr : groups)
		{
			int flat = 0, under = 0, mirrored = 0, above = 0;
			float deep = 0.0f;
			std::map<std::pair<int, int>, int> pages;

			for (const Tri& t : all)
			{
				if (t.group != gr.first)
					continue;

				if (fabsf(t.y - (float)s.y) <= 16.0f)
				{
					if (t.ny > 0.95f)
					{
						flat++;
						pages[std::make_pair(t.page, t.pass)]++;
					}
				}
				else if (t.y > (float)s.y)
				{
					const float my = 2.0f * (float)s.y - t.y;
					bool found = false;

					under++;
					deep = t.y - (float)s.y > deep ? t.y - (float)s.y : deep;

					for (int d = 0; d < 27 && !found; d++)
						found = over.count(cell(
							t.x + 32.0f * (float)(d % 3 - 1),
							my + 32.0f * (float)(d / 3 % 3 - 1),
							t.z + 32.0f * (float)(d / 9 - 1))) != 0;

					mirrored += found ? 1 : 0;
				}
				else
					above++;
			}

			std::string list;

			for (const auto& p : pages)
			{
				char b[32];

				snprintf(b, sizeof(b), "%s%d/%d:%d", list.empty() ? "" : ",",
					p.first.first, p.first.second, p.second);
				list += b;
			}

			printf("wetart tank=%d room=%d x=%d z=%d y=%d group=%d flat=%d "
				"pages=%s under=%d mirrored=%d deep=%.0f over=%d\n", s.item,
				s.room, s.x, s.z, s.y, gr.first, flat,
				list.empty() ? "-" : list.c_str(), under, mirrored, deep,
				above);
		}
	}
}

/* -tankcensus: the art's copy of a room under its floor, for every room of
 * the level - -tankart's question asked of every floor square, wet or not.
 * Per square of a room's classic floor (a wall left out), out of every
 * triangle of the .TRG whatever its group: under it the ones whose centroid
 * lies in the square's column 16 to 2,048 units under the floor, of those
 * the ones mirrored (the centroid mirrored in the floor within 32 units of
 * the centroid of one over it, as -tankart), how deep the deepest lies,
 * and the floor's own faces - flat triangles within 16 units of it - by
 * how the renderer draws them: opaque (pass 0), clear (an alpha-pass
 * triangle on a part-way clear page, HD_PASS_CLEAR, through which what
 * lies under shows), cut (the alpha pass's test), additive. A square
 * counts as copied at 8 mirrored and a third of what lies under, the
 * engine's own numbers (hd_render.cpp, kWetCopyCells) asked of triangles.
 * One `census` line a room - inner squares only, off the ring of walls and
 * doors - and a `censussq` line for every square of a room that has one
 * copied, and for every square any wet floor holds. Where an inner square
 * has no flat face of the art within 16 units of the classic floor, the
 * art's floor stands elsewhere: the flat face nearest to the classic floor
 * within 512 units (two clicks, a free choice) is taken as the floor
 * instead and the square measured again about it - the census line's
 * unseen= (such squares), refit= (an art floor found), refitcopied= (a
 * copy about it) and refitclear= (one under a clear floor). */
static bool g_tank_census = false;

static void ProbeCensus(const std::vector<HDWaterRoom>& rooms,
	const char* root, const char* stem)
{
	if (!g_tank_census)
		return;

	trf::remaster::Trg trg;
	std::string error;

	if (!trf::remaster::read_trg(std::string(root) + "\\DATA\\" + stem +
		".TRG", trg, error))
	{
		printf("census: no .TRG (%s)\n", error.c_str());
		return;
	}

	struct Tri
	{
		float x, y, z;
		int kind;	// -1 not flat, else 0 opaque, 1 clear, 2 cut, 3 add
	};

	std::map<long long, std::vector<Tri> > column;
	const auto key = [](int sx, int sz)
	{
		return ((long long)sx << 32) ^ (long long)(unsigned int)sz;
	};
	const auto cell = [](float x, float y, float z)
	{
		return ((long long)((int)floorf(x / 32.0f) & 0xFFFFF) << 40) |
			((long long)((int)floorf(y / 32.0f) & 0xFFFFF) << 20) |
			(long long)((int)floorf(z / 32.0f) & 0xFFFFF);
	};
	size_t total = 0;

	for (size_t g = 0; g < trg.groups.size() && g < g_boxes_built.size(); g++)
		for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
			for (int pass = 0; pass < 3; pass++)
				for (unsigned int i = 0; i + 2 < part.pass[pass].count; i += 3)
				{
					const unsigned int first = part.pass[pass].start + i;
					float p[3][3];
					int page = -1;

					for (int v = 0; v < 3; v++)
					{
						const trf::remaster::TrgVertex& tv =
							trg.vertices[trg.indices[first + v]];

						p[v][0] = (float)tv.x + (float)g_boxes_built[g].originX;
						p[v][1] = (float)tv.y;
						p[v][2] = (float)tv.z + (float)g_boxes_built[g].originZ;
						page = tv.layer && tv.layer <= trg.textures.size() ?
							(int)trg.textures[tv.layer - 1u] : -1;
					}

					const float ax = p[1][0] - p[0][0], ay = p[1][1] - p[0][1];
					const float az = p[1][2] - p[0][2], bx = p[2][0] - p[0][0];
					const float by = p[2][1] - p[0][1], bz = p[2][2] - p[0][2];
					const float nx = ay * bz - az * by, ny = az * bx - ax * bz;
					const float nz = ax * by - ay * bx;
					const float l = sqrtf(nx * nx + ny * ny + nz * nz);
					Tri t = { (p[0][0] + p[1][0] + p[2][0]) / 3.0f,
						(p[0][1] + p[1][1] + p[2][1]) / 3.0f,
						(p[0][2] + p[1][2] + p[2][2]) / 3.0f, -1 };

					const int slot = HDPageCacheSlotOf(page);

					if (l > 0.0f && fabsf(ny) / l > 0.95f)
						t.kind = pass == 0 ? 0 : pass == 2 ? 3 :
							HDPageCacheSlotClear(slot) ? 1 : 2;

					column[key((int)floorf(t.x / 1024.0f),
						(int)floorf(t.z / 1024.0f))].push_back(t);
					total++;
				}

	// The squares the wet floors hold, to be listed whatever they carry.
	std::set<long long> wet;
	HDWaterWetSquare ws;

	for (int f = 0; f < 8; f++)
		for (int at = 0; HDRenderWaterWetSquare(f, at, &ws); at++)
			if (ws.kept)
				wet.insert(key(ws.x >> 10, ws.z >> 10) ^
					((long long)ws.y << 48));

	struct Square
	{
		int x, z, y, under, mirrored, deep, faces[4];
		bool inner, copied, listed;
	};

	/* The column's art against a floor at fy: the floor's own faces, and
	 * what lies under it, mirrored or not (as -tankart). */
	auto measure = [&](const std::vector<Tri>& all, float fy, Square& s)
	{
		std::set<long long> over;

		for (const Tri& t : all)
			if (t.y < fy - 16.0f && t.y >= fy - 2048.0f - 64.0f)
				over.insert(cell(t.x, t.y, t.z));

		for (const Tri& t : all)
		{
			if (fabsf(t.y - fy) <= 16.0f)
			{
				if (t.kind >= 0)
					s.faces[t.kind]++;
			}
			else if (t.y > fy && t.y <= fy + 2048.0f)
			{
				const float my = 2.0f * fy - t.y;
				bool found = false;

				s.under++;
				s.deep = (int)(t.y - fy) > s.deep ? (int)(t.y - fy) :
					s.deep;

				for (int d = 0; d < 27 && !found; d++)
					found = over.count(cell(
						t.x + 32.0f * (float)(d % 3 - 1),
						my + 32.0f * (float)(d / 3 % 3 - 1),
						t.z + 32.0f * (float)(d / 9 - 1))) != 0;

				s.mirrored += found ? 1 : 0;
			}
		}

		s.copied = s.mirrored >= 8 && s.mirrored * 3 >= s.under;
	};
	int unseen_all = 0, refit_all = 0, refit_copied_all = 0;

	int rooms_copied = 0, squares_all = 0, copied_all = 0, clear_all = 0;

	printf("census level=%s triangles=%u\n", stem, (unsigned)total);

	for (size_t r = 0; r < rooms.size(); r++)
	{
		const HDWaterRoom& w = rooms[r];
		std::vector<Square> sq;

		for (int sx = 0; w.floor && sx < w.sizeX; sx++)
			for (int sz = 0; sz < w.sizeZ; sz++)
			{
				const int at = sx * w.sizeZ + sz;

				if (w.floor[at] == -127)
					continue;

				Square s = { w.originX + sx * 1024, w.originZ + sz * 1024,
					(int)w.floor[at] * 256, 0, 0, 0, { 0, 0, 0, 0 },
					sx > 0 && sz > 0 && sx < w.sizeX - 1 && sz < w.sizeZ - 1,
					false, false };
				const std::vector<Tri>& all = column[key(s.x >> 10, s.z >> 10)];

				measure(all, (float)s.y, s);
				s.listed = wet.count(key(s.x >> 10, s.z >> 10) ^
					((long long)s.y << 48)) != 0;
				sq.push_back(s);
			}

		int squares = 0, copied = 0, clear = 0, under = 0, mirrored = 0;
		int deep = 0, faces[4] = { 0, 0, 0, 0 }, kinds[4] = { 0, 0, 0, 0 };
		int unseen = 0, refit = 0, refitCopied = 0, refitClear = 0;
		bool any = false;

		// The inner squares the art's floor faces were not found at.
		for (const Square& s : sq)
		{
			if (!s.inner || s.faces[0] || s.faces[1] || s.faces[2] ||
				s.faces[3])
				continue;

			const std::vector<Tri>& all = column[key(s.x >> 10, s.z >> 10)];
			float best = 1.0e9f;

			for (const Tri& t : all)
				if (t.kind >= 0 && fabsf(t.y - (float)s.y) <= 512.0f &&
					fabsf(t.y - (float)s.y) < fabsf(best - (float)s.y))
					best = t.y;

			unseen++;

			if (best > 1.0e8f)
				continue;

			Square again = s;

			again.under = again.mirrored = again.deep = 0;

			for (int k = 0; k < 4; k++)
				again.faces[k] = 0;

			measure(all, best, again);
			refit++;
			refitCopied += again.copied ? 1 : 0;
			refitClear += again.copied && again.faces[1] ? 1 : 0;
		}

		unseen_all += unseen;
		refit_all += refit;
		refit_copied_all += refitCopied;

		for (const Square& s : sq)
		{
			any = any || s.copied;

			if (!s.inner)
				continue;

			squares++;
			copied += s.copied ? 1 : 0;
			clear += s.copied && s.faces[1] ? 1 : 0;
			under += s.under;
			mirrored += s.mirrored;
			deep = s.deep > deep ? s.deep : deep;

			for (int k = 0; k < 4; k++)
			{
				faces[k] += s.faces[k];
				kinds[k] += s.faces[k] ? 1 : 0;
			}
		}

		if (!w.floor)
			continue;

		squares_all += squares;
		copied_all += copied;
		clear_all += clear;
		rooms_copied += copied ? 1 : 0;
		printf("census room=%d drawn=%d squares=%d copied=%d copiedclear=%d "
			"under=%d mirrored=%d deep=%d floor=opaque:%d,clear:%d,cut:%d,"
			"add:%d squaresby=opaque:%d,clear:%d,cut:%d,add:%d unseen=%d "
			"refit=%d refitcopied=%d refitclear=%d\n", (int)r, w.drawn,
			squares, copied, clear, under, mirrored, deep, faces[0],
			faces[1], faces[2], faces[3], kinds[0], kinds[1], kinds[2],
			kinds[3], unseen, refit, refitCopied, refitClear);

		for (const Square& s : sq)
			if (any || s.listed)
				printf("censussq room=%d x=%d z=%d y=%d %s under=%d "
					"mirrored=%d deep=%d floor=opaque:%d,clear:%d,cut:%d,"
					"add:%d copy=%s%s\n", (int)r, s.x, s.z, s.y,
					s.inner ? "inner" : "ring", s.under, s.mirrored, s.deep,
					s.faces[0], s.faces[1], s.faces[2], s.faces[3],
					s.copied ? "yes" : "no", s.listed ? " wet" : "");
	}

	printf("census level=%s rooms=%d squares=%d copied=%d copiedclear=%d "
		"roomscopied=%d unseen=%d refit=%d refitcopied=%d\n", stem,
		(int)rooms.size(), squares_all, copied_all, clear_all, rooms_copied,
		unseen_all, refit_all, refit_copied_all);
}

/* -marblecensus: which floor squares are polished stone, for every room of
 * the level. Per inner square of a room's classic floor (off its ring of
 * walls and doors, not a wall): the classic footstep material (FLOOR_INFO's
 * fx, 9 marble); the art's floor - the .TRG's flat triangles within 16
 * units of the classic floor, as -tankcensus takes them, or where none
 * lies there about the flat face nearest within 512 units (refit, as
 * -tankcensus refits) - by the page and the sheen's class that cover most
 * of its area (hd_gloss_math.h: the part's material, else the page as
 * HDRenderGlossPageInfo resolves it) and by how the renderer draws most of
 * its faces (opaque, clear, cut, additive); whether the page is one of the
 * lobby's barred ones (HDGlossPageBarred); and the art's own copy under the
 * floor (-tankcensus' rule). A square is a candidate when its floor is
 * polished stone (MarbleStone). It is a mirror square - the rule this
 * census proposes - when it is a candidate, its faces are opaque, no copy
 * shows through a clear floor (a copy under an opaque floor does not
 * show) and its room is no water room. One marblesq line a candidate,
 * one marbleroom line a room with a candidate, one marblepage line a page
 * on a candidate's floor, one marblefloor line a page any inner square's
 * floor wears most, and one marblelevel line. */
static bool g_marble_census = false;
// -marblequiet: the census without its marblesq lines.
static bool g_marble_quiet = false;

/* A floor square of the census: its room, corner, classic floor y and the
 * art's floor y (ay, the classic one unless refit), footstep material,
 * page, the sheen's class, how its faces are drawn and how many. */
struct MarbleSquare
{
	int room, x, z, y, ay, fx, page, cls, kind, faces;
	bool barred, copied, clear, mirror, refit;
};

/* -marblepages a,b,...: the pages a floor of polished stone wears, in place
 * of what the data says of a floor (MarbleStone). */
static std::set<int> g_marble_pages;

/* Whether a floor is polished stone: its page is on -marblepages' list,
 * or without a list by what the data says of it - its page resolves to a
 * dielectric sheen (marble, ice, glass; material 3 resolves to glass),
 * its page is one of RICH2's lobby pages the sheen bars (hd_gloss_math.h:
 * its marble), or its classic square's footstep material is marble (fx
 * 9). */
static bool MarbleStone(int page, int cls, bool barred, int fx)
{
	if (!g_marble_pages.empty())
		return g_marble_pages.count(page) != 0;

	return cls == HD_GLOSS_MARBLE || cls == HD_GLOSS_ICE ||
		cls == HD_GLOSS_GLASS || barred || fx == 9;
}

static void ProbeMarbleCensus(const std::vector<HDWaterRoom>& rooms,
	const char* root, const char* stem)
{
	if (!g_marble_census)
		return;

	trf::remaster::Trg trg;
	std::string error;

	if (!trf::remaster::read_trg(std::string(root) + "\\DATA\\" + stem +
		".TRG", trg, error))
	{
		printf("marble: no .TRG (%s)\n", error.c_str());
		return;
	}

	// The classes the sheen resolved, by page.
	std::map<int, int> classOf;
	HDRenderGlossPage gp;

	for (int at = 0; HDRenderGlossPageInfo(at, &gp); at++)
		if (gp.strength > 0.0f && !classOf.count(gp.page))
			classOf[gp.page] = gp.cls;

	/* A triangle by its centroid, its area, how it is drawn (-1 not flat,
	 * 0 opaque, 1 clear, 2 cut, 3 additive), its page and the class of the
	 * sheen its part's material or its page names. */
	struct Tri
	{
		float x, y, z, area;
		int kind, page, cls;
	};

	std::map<long long, std::vector<Tri> > column;
	const auto key = [](int sx, int sz)
	{
		return ((long long)sx << 32) ^ (long long)(unsigned int)sz;
	};
	const auto cell = [](float x, float y, float z)
	{
		return ((long long)((int)floorf(x / 32.0f) & 0xFFFFF) << 40) |
			((long long)((int)floorf(y / 32.0f) & 0xFFFFF) << 20) |
			(long long)((int)floorf(z / 32.0f) & 0xFFFFF);
	};

	for (size_t g = 0; g < trg.groups.size() && g < g_boxes_built.size(); g++)
		for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
		{
			HDGloss mat;
			const bool own = HDGlossFromMaterial(part.material, part.param,
				&mat);

			for (int pass = 0; pass < 3; pass++)
				for (unsigned int i = 0; i + 2 < part.pass[pass].count; i += 3)
				{
					const unsigned int first = part.pass[pass].start + i;
					float p[3][3];
					int page = -1;

					for (int v = 0; v < 3; v++)
					{
						const trf::remaster::TrgVertex& tv =
							trg.vertices[trg.indices[first + v]];

						p[v][0] = (float)tv.x + (float)g_boxes_built[g].originX;
						p[v][1] = (float)tv.y;
						p[v][2] = (float)tv.z + (float)g_boxes_built[g].originZ;
						page = tv.layer && tv.layer <= trg.textures.size() ?
							(int)trg.textures[tv.layer - 1u] : -1;
					}

					const float ax = p[1][0] - p[0][0], ay = p[1][1] - p[0][1];
					const float az = p[1][2] - p[0][2], bx = p[2][0] - p[0][0];
					const float by = p[2][1] - p[0][1], bz = p[2][2] - p[0][2];
					const float nx = ay * bz - az * by, ny = az * bx - ax * bz;
					const float nz = ax * by - ay * bx;
					const float l = sqrtf(nx * nx + ny * ny + nz * nz);
					Tri t = { (p[0][0] + p[1][0] + p[2][0]) / 3.0f,
						(p[0][1] + p[1][1] + p[2][1]) / 3.0f,
						(p[0][2] + p[1][2] + p[2][2]) / 3.0f, 0.5f * l, -1,
						page, own ? mat.cls : classOf.count(page) ?
						classOf[page] : HD_GLOSS_NONE };

					if (l > 0.0f && fabsf(ny) / l > 0.95f)
						t.kind = pass == 0 ? 0 : pass == 2 ? 3 :
							HDPageCacheSlotClear(HDPageCacheSlotOf(page)) ?
							1 : 2;

					column[key((int)floorf(t.x / 1024.0f),
						(int)floorf(t.z / 1024.0f))].push_back(t);
				}
		}

	struct PageSum
	{
		int squares, mirror, fx9, cls;
		bool barred, clear;
		std::set<int> rooms;
	};

	struct FloorSum
	{
		int squares, fx[16], kinds[5], cls, copied;
		std::set<int> rooms;
	};

	std::map<int, PageSum> pages;
	std::map<int, FloorSum> floors;
	int all = 0, cand = 0, mirror = 0, fx9 = 0, byPage = 0, both = 0;
	int roomsMirror = 0, refit = 0;

	/* The square's art floor at fy: its faces' page by area, their kind by
	 * count, and the copy under it (-tankcensus' rule); false where no flat
	 * face lies within 16 units of fy. */
	const auto measure = [&](const std::vector<Tri>& col, float fy,
		MarbleSquare& s)
	{
		std::map<long long, float> area;
		std::map<long long, int> clsOf;
		int byKind[4] = { 0, 0, 0, 0 };
		std::set<long long> over;
		int under = 0, mirrored = 0;

		s.faces = 0;
		s.page = -1;
		s.kind = -1;
		s.cls = HD_GLOSS_NONE;

		for (const Tri& t : col)
			if (t.y < fy - 16.0f && t.y >= fy - 2048.0f - 64.0f)
				over.insert(cell(t.x, t.y, t.z));

		for (const Tri& t : col)
		{
			if (fabsf(t.y - fy) <= 16.0f && t.kind >= 0)
			{
				const long long k = ((long long)t.page << 8) | t.cls;

				area[k] += t.area;
				byKind[t.kind]++;
				s.faces++;
			}
			else if (t.y > fy + 16.0f && t.y <= fy + 2048.0f)
			{
				const float my = 2.0f * fy - t.y;
				bool found = false;

				under++;

				for (int d = 0; d < 27 && !found; d++)
					found = over.count(cell(
						t.x + 32.0f * (float)(d % 3 - 1),
						my + 32.0f * (float)(d / 3 % 3 - 1),
						t.z + 32.0f * (float)(d / 9 - 1))) != 0;

				mirrored += found ? 1 : 0;
			}
		}

		float best = 0.0f;

		for (const std::pair<const long long, float>& a : area)
			if (a.second > best)
			{
				best = a.second;
				s.page = (int)(a.first >> 8);
				s.cls = (int)(a.first & 255);
			}

		for (int k = 3; k >= 0; k--)
			if (byKind[k] && (s.kind < 0 || byKind[k] >= byKind[s.kind]))
				s.kind = k;

		s.copied = mirrored >= 8 && mirrored * 3 >= under;
		s.clear = byKind[1] > 0;
		return s.faces > 0;
	};

	for (size_t r = 0; r < rooms.size(); r++)
	{
		const HDWaterRoom& w = rooms[r];
		const HDRoomGrid* grid = r < g_grids_built.size() ?
			&g_grids_built[r] : 0;

		if (!w.floor)
			continue;

		int squares = 0, rc = 0, rm = 0, rfx = 0, rcopied = 0, rrefit = 0;
		int kinds[4] = { 0, 0, 0, 0 };
		int x0 = 1 << 30, z0 = 1 << 30, x1 = -(1 << 30), z1 = -(1 << 30);
		std::set<int> heights, rpages;

		for (int sx = 1; sx < w.sizeX - 1; sx++)
			for (int sz = 1; sz < w.sizeZ - 1; sz++)
			{
				const int at = sx * w.sizeZ + sz;

				if (w.floor[at] == -127)
					continue;

				MarbleSquare s = {};

				s.room = (int)r;
				s.x = w.originX + sx * 1024;
				s.z = w.originZ + sz * 1024;
				s.y = (int)w.floor[at] * 256;
				s.ay = s.y;

				if (grid && grid->zSectors == w.sizeZ &&
					grid->first + at < (int)g_sectors_built.size())
					s.fx = g_sectors_built[(size_t)(grid->first + at)].fx;

				const std::vector<Tri>& col = column[key(s.x >> 10,
					s.z >> 10)];

				/* No flat face of the art at the classic floor: the art's
				 * floor stands elsewhere, the flat face nearest within 512
				 * units, as -tankcensus refits it. */
				if (!measure(col, (float)s.y, s))
				{
					float nearY = 1.0e9f;

					for (const Tri& t : col)
						if (t.kind >= 0 && fabsf(t.y - (float)s.y) <= 512.0f &&
							fabsf(t.y - (float)s.y) < fabsf(nearY - (float)s.y))
							nearY = t.y;

					if (nearY < 1.0e8f && measure(col, nearY, s))
					{
						s.ay = (int)lroundf(nearY);
						s.refit = true;
					}
				}

				s.barred = s.page >= 0 && HDGlossPageBarred(s.page);
				s.mirror = MarbleStone(s.page, s.cls, s.barred, s.fx) &&
					s.kind == 0 && !(s.copied && s.clear) &&
					!(w.flags & HD_WATER_ROOM_UNDERWATER);
				squares++;
				all++;
				refit += s.refit ? 1 : 0;
				rrefit += s.refit ? 1 : 0;

				const bool isCand = MarbleStone(s.page, s.cls, s.barred,
					s.fx);

				// Every inner square's floor by its page, candidate or not.
				FloorSum& fs = floors[s.page];

				fs.squares++;
				fs.fx[s.fx & 15]++;
				fs.kinds[s.kind < 0 ? 4 : s.kind]++;
				fs.cls = s.cls > fs.cls ? s.cls : fs.cls;
				fs.copied += s.copied ? 1 : 0;
				fs.rooms.insert((int)r);

				if (!isCand)
					continue;

				cand++;
				rc++;
				fx9 += s.fx == 9 ? 1 : 0;
				rfx += s.fx == 9 ? 1 : 0;
				byPage += s.cls != HD_GLOSS_NONE ? 1 : 0;
				both += s.cls != HD_GLOSS_NONE && s.fx == 9 ? 1 : 0;
				mirror += s.mirror ? 1 : 0;
				rm += s.mirror ? 1 : 0;
				rcopied += s.copied ? 1 : 0;

				if (s.kind >= 0)
					kinds[s.kind]++;

				if (s.mirror)
				{
					heights.insert(s.ay);
					x0 = s.x < x0 ? s.x : x0;
					z0 = s.z < z0 ? s.z : z0;
					x1 = s.x + 1024 > x1 ? s.x + 1024 : x1;
					z1 = s.z + 1024 > z1 ? s.z + 1024 : z1;
				}

				if (s.page >= 0)
				{
					PageSum& ps = pages[s.page];

					rpages.insert(s.page);
					ps.squares++;
					ps.mirror += s.mirror ? 1 : 0;
					ps.fx9 += s.fx == 9 ? 1 : 0;
					ps.cls = s.cls;
					ps.barred = s.barred;
					ps.clear = ps.clear || s.kind == 1;
					ps.rooms.insert((int)r);
				}

				if (g_marble_quiet)
					continue;

				printf("marblesq level=%s room=%d x=%d z=%d y=%d art=%d fx=%d "
					"page=%d class=%s barred=%d floor=%s faces=%d copy=%s "
					"water=%d mirror=%d\n", stem, (int)r, s.x, s.z, s.y, s.ay,
					s.fx,
					s.page, HDGlossClassName(s.cls), s.barred ? 1 : 0,
					s.kind == 0 ? "opaque" : s.kind == 1 ? "clear" :
					s.kind == 2 ? "cut" : s.kind == 3 ? "add" : "none",
					s.faces, s.copied ? "yes" : "no",
					w.flags & HD_WATER_ROOM_UNDERWATER ? 1 : 0,
					s.mirror ? 1 : 0);
			}

		if (!rc)
			continue;

		roomsMirror += rm ? 1 : 0;

		std::string hs, pl;

		for (int h : heights)
			hs += (hs.empty() ? "" : ",") + std::to_string(h);

		for (int p : rpages)
			pl += (pl.empty() ? "" : ",") + std::to_string(p);

		printf("marbleroom level=%s room=%d drawn=%d squares=%d candidates=%d "
			"fx9=%d mirror=%d copied=%d refit=%d floor=opaque:%d,clear:%d,"
			"cut:%d,add:%d heights=%s pages=%s box=%d..%d,%d..%d\n", stem,
			(int)r, w.drawn, squares, rc, rfx, rm, rcopied, rrefit, kinds[0],
			kinds[1], kinds[2], kinds[3], hs.empty() ? "-" : hs.c_str(),
			pl.empty() ? "-" : pl.c_str(), rm ? x0 : 0, rm ? x1 : 0,
			rm ? z0 : 0, rm ? z1 : 0);
	}

	for (const std::pair<const int, PageSum>& p : pages)
	{
		std::string rl;

		for (int r : p.second.rooms)
			rl += (rl.empty() ? "" : ",") + std::to_string(r);

		printf("marblepage level=%s page=%d class=%s barred=%d clear=%d "
			"squares=%d fx9=%d mirror=%d rooms=%s\n", stem, p.first,
			HDGlossClassName(p.second.cls), p.second.barred ? 1 : 0,
			p.second.clear ? 1 : 0, p.second.squares, p.second.fx9,
			p.second.mirror, rl.c_str());
	}

	/* One marblefloor line a page any inner square's floor wears most: its
	 * squares, their footstep materials (fx:count), how its faces are drawn
	 * (opaque, clear, cut, additive, none), its class, the squares with a
	 * copy under them and its rooms. */
	for (const std::pair<const int, FloorSum>& f : floors)
	{
		std::string fx, rl;

		for (int k = 0; k < 16; k++)
			if (f.second.fx[k])
				fx += (fx.empty() ? "" : ",") + std::to_string(k) + ":" +
					std::to_string(f.second.fx[k]);

		for (int r : f.second.rooms)
			rl += (rl.empty() ? "" : ",") + std::to_string(r);

		printf("marblefloor level=%s page=%d squares=%d fx=%s kinds=%d,%d,%d,"
			"%d,%d class=%s copied=%d rooms=%s\n", stem, f.first,
			f.second.squares, fx.c_str(), f.second.kinds[0],
			f.second.kinds[1], f.second.kinds[2], f.second.kinds[3],
			f.second.kinds[4], HDGlossClassName(f.second.cls),
			f.second.copied, rl.c_str());
	}

	printf("marblelevel level=%s squares=%d candidates=%d fx9=%d class=%d "
		"both=%d mirror=%d roomsmirror=%d refit=%d\n", stem, all, cand, fx9,
		byPage, both, mirror, roomsMirror, refit);
}

/* The marble floors' switches, out of ProbeMain's chain of else-ifs (the
 * usage at the top, after -tankcensus). -marblecensus needs -roombox and
 * builds the classic floor as -hdwater 0 does where -hdwater is not given;
 * -marble, -marbleplanes and -marblerows set the engine's marble floors. */
/* -marble 0|1 and -marbleplanes n: the engine's marble floors and the most
 * floor heights a frame mirrors, as tomb5.hd_marble and hd_marble_planes;
 * off where -marble is not given, so a picture of another switch stays the
 * one it was. */
static int g_marble_engine = 0;
static int g_marble_planes = 2;

static bool MarbleArg(int argc, char** argv, int* at)
{
	const char* a = argv[*at];
	const bool more = *at + 1 < argc;

	if (strncmp(a, "-marble", 7))
		return false;

	/* The engine's own marble floors (ProbeMarble), set before the build;
	 * the faces are built where the water is, with the classic level read
	 * as -hdwater 0 reads it where -hdwater is not given. */
	if ((!strcmp(a, "-marble") || !strcmp(a, "-marbleplanes")) && more)
	{
		(a[7] ? g_marble_planes : g_marble_engine) = atoi(argv[++*at]);
		HDRenderMarbleSet(g_marble_engine, g_marble_planes);

		if (g_marble_engine && g_hdwater < 0)
			g_hdwater = 0;

		return true;
	}

	// -marblerows 1|2: the mirror target's rows a row of the frame.
	if (!strcmp(a, "-marblerows") && more)
	{
		HDRenderMarbleRows(atoi(argv[++*at]));
		return true;
	}

	// -marbleclip <units>: the mirrored draw's start over the floor.
	if (!strcmp(a, "-marbleclip") && more)
	{
		HDRenderMarbleClip((float)atof(argv[++*at]));
		return true;
	}

	// -marblecopies 0|1: 1 keeps the art's copies under the floors.
	if (!strcmp(a, "-marblecopies") && more)
	{
		HDRenderMarbleKeepCopies(atoi(argv[++*at]) != 0);
		return true;
	}

	if (!strcmp(a, "-marblecensus") || !strcmp(a, "-marblequiet"))
		g_marble_quiet = g_marble_quiet || a[7] == 'q';
	else if (!strcmp(a, "-marblepages") && more)
	{
		for (const char* s = argv[++*at]; *s; s++)
		{
			g_marble_pages.insert(atoi(s));

			while (s[1] && *s != ',')
				s++;
		}
	}
	else
		return false;

	g_marble_census = true;

	if (g_hdwater < 0)
		g_hdwater = 0;

	return true;
}


// A timestamp bracket on the GPU: Begin, then End returns its ms (-1).
struct MarbleTimer
{
	IDirect3DQuery9* q[4] = { 0, 0, 0, 0 };

	void Begin()
	{
		g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &q[0]);
		g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1]);
		g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2]);
		g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &q[3]);

		if (q[0] && q[1] && q[2] && q[3])
		{
			q[0]->Issue(D3DISSUE_BEGIN);
			q[1]->Issue(D3DISSUE_END);
		}
	}

	double End()
	{
		double ms = -1.0;

		if (q[0] && q[1] && q[2] && q[3])
		{
			UINT64 a = 0, b = 0, f = 0;
			BOOL disjoint = TRUE;

			q[2]->Issue(D3DISSUE_END);
			q[0]->Issue(D3DISSUE_END);
			q[3]->Issue(D3DISSUE_END);

			while (q[0]->GetData(&disjoint, sizeof(disjoint),
				D3DGETDATA_FLUSH) == S_FALSE)
				;

			while (q[1]->GetData(&a, sizeof(a), D3DGETDATA_FLUSH) == S_FALSE)
				;

			while (q[2]->GetData(&b, sizeof(b), D3DGETDATA_FLUSH) == S_FALSE)
				;

			while (q[3]->GetData(&f, sizeof(f), D3DGETDATA_FLUSH) == S_FALSE)
				;

			if (!disjoint && f)
				ms = (double)(b - a) * 1000.0 / (double)f;
		}

		for (int i = 0; i < 4; i++)
			if (q[i])
			{
				q[i]->Release();
				q[i] = 0;
			}

		return ms;
	}
};

/* -tankwetmap: floor `floor`'s squares, room by room, one line each: the
 * square's corner, its floor, its nearest point's way from the glass in
 * units and in squares, the moment the front first reaches it (the dome's
 * own out to its foot, the flood's speed past it), whether the radius
 * keeps it and, as copy=, the art's copy of the room under it: the cells
 * of art under the floor, the mirrored ones and whether the square counts
 * as copied (HDWaterWetSquare); clear= 1 where its floor is a clear floor,
 * the one kind of floor a copy counts under. */
static void ProbeWetMap(int floor)
{
	if (!g_tank_wet_map)
		return;

	std::vector<HDWaterWetSquare> all;
	HDWaterWetSquare s;

	for (int at = 0; HDRenderWaterWetSquare(floor, at, &s); at++)
		all.push_back(s);

	std::stable_sort(all.begin(), all.end(),
		[](const HDWaterWetSquare& a, const HDWaterWetSquare& b)
		{
			return a.room < b.room;
		});

	for (const HDWaterWetSquare& q : all)
	{
		const float t = q.way <= q.reach ?
			kTankDomeOut * q.way / (q.reach > 0.0f ? q.reach : 1.0f) :
			kTankDomeOut + (q.way - q.reach) / kWetSpread;

		printf("wetmap tank=%d room=%d x=%d z=%d y=%d way=%.0f squares=%.2f "
			"t=%.2f %s copy=%d/%d/%s slope=%d clear=%d\n", q.item, q.room,
			q.x, q.z, q.y, q.way, q.way / 1024.0f, t,
			q.kept ? "kept" : "dropped", q.under, q.mirrored,
			q.copied ? "yes" : "no", q.slope, q.clear);
	}
}

static void WetLaraMul(const float* a, const float* b, float* out)
{
	float t[16];

	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			t[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] +
				a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];

	memcpy(out, t, sizeof(t));
}

// A turn about y (yaw) or x (pitch) in degrees, ModelProbe's MatRotY/X.
static void WetLaraTurn(int axis, float deg, float* m)
{
	const float a = deg * 3.14159265f / 180.0f, c = cosf(a), s = sinf(a);

	memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1.0f;

	if (axis == 0)
	{
		m[0] = c; m[2] = -s; m[8] = s; m[10] = c;
	}
	else
	{
		m[5] = c; m[6] = s; m[9] = -s; m[10] = c;
	}
}

/* Her node matrices, model space to `view`: the classic stack walk with a
 * pitch per node where the pose asks for one (ModelProbe's
 * BuildNodeMatrices), the whole turned by yaw and set on her floor. */
static void WetLaraMatrices(const float* view)
{
	static const float kCrawl[HD_LARA_NODES] =
	{
		-90.0f, 90.0f, -90.0f, 0.0f, 90.0f, -90.0f, 0.0f, 0.0f,
		90.0f, 0.0f, 0.0f, 90.0f, 0.0f, 0.0f, 0.0f
	};
	float local[HD_LARA_NODES * 16], stack[HD_LARA_NODES][16];
	float current[16], step[16], turn[16];
	int top = 0;

	WetLaraTurn(1, g_wet_lara_pose ? kCrawl[0] : 0.0f, current);
	memcpy(local, current, sizeof(current));

	for (int n = 1; n < HD_LARA_NODES; n++)
	{
		if ((g_wet_lara_flags[n] & 1) && top > 0)
			memcpy(current, stack[--top], sizeof(current));

		if (g_wet_lara_flags[n] & 2)
		{
			memcpy(stack[top], current, sizeof(current));

			if (top < HD_LARA_NODES - 1)
				++top;
		}

		memset(step, 0, sizeof(step));
		step[0] = step[5] = step[10] = step[15] = 1.0f;
		step[12] = g_wet_lara_bind[n][0];
		step[13] = g_wet_lara_bind[n][1];
		step[14] = g_wet_lara_bind[n][2];
		WetLaraMul(step, current, current);

		if (g_wet_lara_pose && kCrawl[n] != 0.0f)
		{
			WetLaraTurn(1, kCrawl[n], turn);
			WetLaraMul(turn, current, current);
		}

		memcpy(local + n * 16, current, sizeof(current));
	}

	// Her lowest joint 40 units over her floor (y runs down).
	float lowest = -1.0e9f;

	for (int n = 0; n < HD_LARA_NODES; n++)
		lowest = local[n * 16 + 13] > lowest ? local[n * 16 + 13] : lowest;

	float place[16], yaw[16];

	WetLaraTurn(0, g_wet_lara_at[3], yaw);
	memset(place, 0, sizeof(place));
	place[0] = place[5] = place[10] = place[15] = 1.0f;
	place[12] = g_wet_lara_at[0] - g_seed_origin[0];
	place[13] = g_wet_lara_at[1] - 40.0f - lowest;
	place[14] = g_wet_lara_at[2] - g_seed_origin[1];
	WetLaraMul(yaw, place, place);
	WetLaraMul(place, view, place);

	for (int n = 0; n < HD_LARA_NODES; n++)
		WetLaraMul(local + n * 16, place, g_wet_lara_mats + n * 16);
}

// The slot of a name in the engine's own table, or -1.
static int WetLaraSlot(const char* want)
{
	for (int s = 0; s < 1024; s++)
		if (HDSlotName(s) && !_stricmp(HDSlotName(s), want))
			return s;

	return -1;
}

/* Her outfit out of the level's header and her skeleton out of its mesh
 * tree (ModelProbe's RunLara and ReadClassicSlot), built on the card. */
static void WetLaraBuild(const char* root, const char* stem)
{
	Tr5Level level;
	std::string error;
	const std::string trc = g_roombox + "\\" + stem + ".TRC";
	std::wstring wide(trc.begin(), trc.end());
	const char* why = "";

	if (g_roombox.empty() || !level.load(wide, error))
	{
		printf("-wetlara: %s not read - no Lara\n", trc.c_str());
		return;
	}

	int body = -1, hair = -1;
	Tr5Level::MoveableInfo info;

	for (int s : level.moveableSlots())
	{
		const char* name = HDSlotName(s);

		if (name && !_stricmp(name, "HAIR"))
			hair = s;

		if (name && (!_stricmp(name, "LARA_SKIN") ||
			(body < 0 && !_stricmp(name, "LARA"))))
			body = s;
	}

	if (body < 0 || !level.moveableInfo(body, info) ||
		info.nmeshes < HD_LARA_NODES)
	{
		printf("-wetlara: no Lara mesh tree in %s\n", stem);
		return;
	}

	// The bind walk: four longs a node after the first, flags x y z.
	float bindAbs[HD_LARA_NODES][3] = {};
	float cur[3] = { 0, 0, 0 }, st[HD_LARA_NODES][3];
	int top = 0;

	for (int n = 1; n < HD_LARA_NODES; n++)
	{
		const long long at = (long long)info.boneIndex + (n - 1) * 4LL;

		g_wet_lara_flags[n] = level.debugReadBoneLong(at);

		for (int k = 0; k < 3; k++)
			g_wet_lara_bind[n][k] = (float)level.debugReadBoneLong(at + 1 + k);

		if ((g_wet_lara_flags[n] & 1) && top > 0)
			memcpy(cur, st[--top], sizeof(cur));

		if (g_wet_lara_flags[n] & 2)
		{
			memcpy(st[top], cur, sizeof(cur));

			if (top < HD_LARA_NODES - 1)
				++top;
		}

		for (int k = 0; k < 3; k++)
			bindAbs[n][k] = cur[k] += g_wet_lara_bind[n][k];
	}

	const int drawType = level.laraType() + 1;
	const bool joby = !_strnicmp(stem, "JOBY", 4);
	const int outfit = HDLaraOutfitFor(drawType, (joby && drawType == 1) ||
		drawType == 3, drawType == 2, level.hdOutfit(), &why);
	std::vector<int> present;

	for (int s : level.moveableSlots())
		if (HDSlotIsLara(s))
			present.push_back(s);

	HDLaraLevel wardrobe;

	wardrobe.outfit = outfit;
	wardrobe.bodyBind = &bindAbs[0][0];
	wardrobe.bodyNodes = HD_LARA_NODES;
	wardrobe.hairBind = 0;
	wardrobe.hairNodes = 0;
	wardrobe.slots = present.empty() ? 0 : &present[0];
	wardrobe.slotCount = (int)present.size();
	(void)hair;

	HDModelSlot wanted[HD_LARA_PART_COUNT * 2];
	const int count = HDLaraPlan(&wardrobe, wanted, HD_LARA_PART_COUNT * 2);
	HDModelStats stats;
	HDLaraStats laraStats;

	if (!HDModelInit(g_device, &why) || count <= 0 ||
		!HDModelLevelBegin(g_device, root, wanted, count, false, 1, &stats,
		&why) || !HDLaraLevelBuild(&wardrobe, &laraStats))
	{
		printf("-wetlara: her build failed - %s\n", why);
		return;
	}

	g_wet_lara_ready = true;
	printf("-wetlara: %s (%s), %d parts planned, %d loaded, at %.0f,%.0f,"
		"%.0f yaw %.0f %s\n", HDLaraOutfitModel(outfit) ?
		HDLaraOutfitModel(outfit) : "none", HDLaraOutfitLabel(outfit), count,
		laraStats.loaded, g_wet_lara_at[0], g_wet_lara_at[1], g_wet_lara_at[2],
		g_wet_lara_at[3], g_wet_lara_pose ? "crawl" : "stand");
}

/* Her parts as HDObjectFrameDraw draws the objects: the body, the head
 * and the face, the holster rig and the hands of a frame with nothing in
 * them, her nodes lit evenly (128, the classic mid shade) under the sun's
 * game factor. */
static void ProbeWetLaraDraw(const void* projection, int bulbs,
	unsigned int passes)
{
	if (!g_wet_lara_ready)
		return;

	float light[HD_LARA_NODES * 3];

	for (int i = 0; i < HD_LARA_NODES * 3; i++)
		light[i] = 128.0f;

	HDModelLighting lighting;

	lighting.lights = 0;
	lighting.lightCount = 0;
	lighting.sunScale = 0.75f;
	lighting.specStrength = 0.0f;
	lighting.specPower = 1.0f;
	HDModelFrameBegin((const D3DMATRIX*)projection, bulbs, passes);
	HDModelClip(0);

	const unsigned int bodyMask = HDLaraBodyMask();
	static const int kParts[4] =
	{
		HD_LARA_PART_BODY, HD_LARA_PART_HEAD, HD_LARA_PART_FACE,
		HD_LARA_PART_HOLSTER_RIG
	};

	for (int i = 0; i < 4; i++)
		if (HDLaraHasPart(kParts[i]))
			HDModelDraw(HDLaraPartSlot(kParts[i]), g_wet_lara_mats,
				HD_LARA_NODES, light, i == 0 ? bodyMask : i == 3 ?
				HDLaraPartCoverage(kParts[i]) : ~0u, &lighting);

	HDLaraFrame now;
	HDLaraDraw list[8];
	const int lara = WetLaraSlot("LARA");
	const int holster = WetLaraSlot("LARA_HOLSTERS_PISTOLS");

	now.rightHandSlot = now.leftHandSlot = lara;
	now.leftHolsterSlot = now.rightHolsterSlot = holster;
	now.backGunSlot = 0;

	const int pieces = HDLaraFrameParts(&now, list, 8);

	for (int i = 0; i < pieces; i++)
		HDModelDraw(HDLaraPartSlot(list[i].part), g_wet_lara_mats,
			HD_LARA_NODES, light, list[i].nodeMask, &lighting);
}

// The stand-ins as HDObjectFrameDraw would draw the objects.
static void ProbeWetStands(const void* projection, int bulbs,
	unsigned int passes)
{
	(void)bulbs;
	(void)passes;

	for (size_t i = 0; i + 2 < g_wet_stands.size(); i += 3)
		HDRenderWaterProbeStand(g_wet_stands[i], g_wet_stands[i + 1],
			g_wet_stands[i + 2], g_seed_origin[0], g_seed_origin[1],
			(const float*)projection, g_wet_stand_alpha);
}

/* The tanks' wet floors where HDFrameDrawGlow draws them: part 0 the
 * darkening before the frame's copy, part 1 the mirror before the cards
 * (its own copy inside the bracket), the picture's world shifted by
 * the seed room's origin as the cards' is, timed on the GPU with a
 * timestamp pair; the lines are -hdwater's, so the timing scripts add
 * them to the water's other phases. */
// -wetagain's split of the mirror's time (set with -wetagain).
static bool g_wet_again_split = false;
// -wetnomirror: the darkening alone.
static bool g_wet_no_mirror = false;

static void ProbeWetDraw(int part)
{
	if (g_hdwater != 1 || g_tank_item < 0)
		return;

	// The processor's time from here to the GPU's end of the draws.
	LARGE_INTEGER cpuFreq, cpu0, cpu1;

	QueryPerformanceFrequency(&cpuFreq);
	QueryPerformanceCounter(&cpu0);

	float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
		-g_seed_origin[0], 0, -g_seed_origin[1], 1 };
	IDirect3DQuery9* q[3] = { 0, 0, 0 };
	IDirect3DQuery9* freq = 0;

	/* The opaque frame's copy the sheets read, taken before the bracket:
	 * the copy waits for its own timestamps (its own line), and that wait
	 * inside the bracket would count as the floor's. */
	if (part == 0 && g_tank_time >= 0.0f)
		HDRenderWaterCopy();

	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &q[0]);
	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1]);
	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2]);
	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &freq);

	const bool timed = q[0] && q[1] && q[2] && freq;

	if (timed)
	{
		q[0]->Issue(D3DISSUE_BEGIN);
		q[1]->Issue(D3DISSUE_END);
	}

	if (part == 0)
		HDRenderWaterWetDraw(m);
	else
	{
		// -wetstand: the stand-ins through the models' mirror.
		if (!g_wet_stands.empty() && !g_wet_no_models)
			HDRenderWaterWetModels(ProbeWetStands, g_wet_stand_proj, 0);

		// -wetlara: Lara through the models' mirror.
		if (g_wet_lara_ready && !g_wet_no_models)
			HDRenderWaterWetModels(ProbeWetLaraDraw, g_wet_stand_proj, 0);

		// -wetagain: the models' share of the processor's time, apart.
		if (g_wet_again_split)
		{
			IDirect3DQuery9* e = 0;
			LARGE_INTEGER now;

			if (SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_EVENT, &e)) && e)
			{
				e->Issue(D3DISSUE_END);

				while (e->GetData(0, 0, D3DGETDATA_FLUSH) == S_FALSE) {}

				e->Release();
			}

			QueryPerformanceCounter(&now);
			printf("-wetagain: the models' mirror cpu %.2f\n",
				(double)(now.QuadPart - cpu0.QuadPart) * 1000.0 /
				(double)cpuFreq.QuadPart);
		}

		if (!g_wet_no_mirror)
			HDRenderWaterWetMirror();
	}

	if (timed)
	{
		q[2]->Issue(D3DISSUE_END);
		q[0]->Issue(D3DISSUE_END);
		freq->Issue(D3DISSUE_END);
	}

	double ms = -1.0;

	if (timed)
	{
		UINT64 a = 0, b = 0, f = 0;
		BOOL disjoint = TRUE;

		while (q[0]->GetData(&disjoint, sizeof(disjoint), D3DGETDATA_FLUSH) ==
			S_FALSE)
			;

		while (q[1]->GetData(&a, sizeof(a), D3DGETDATA_FLUSH) == S_FALSE)
			;

		while (q[2]->GetData(&b, sizeof(b), D3DGETDATA_FLUSH) == S_FALSE)
			;

		while (freq->GetData(&f, sizeof(f), D3DGETDATA_FLUSH) == S_FALSE)
			;

		if (!disjoint && f)
			ms = (double)(b - a) * 1000.0 / (double)f;
	}

	for (int i = 0; i < 3; i++)
		if (q[i])
			q[i]->Release();

	if (freq)
		freq->Release();

	HDRenderSplashStats st;

	HDRenderSplashStatsNow(&st);
	QueryPerformanceCounter(&cpu1);

	const double cpuMs = (double)(cpu1.QuadPart - cpu0.QuadPart) * 1000.0 /
		(double)cpuFreq.QuadPart;

	if (part == 0)
		printf("-hdwater: %d wet floor draw(s), GPU %.4f ms (timestamps round "
			"the wet floor's draw), floor %d wet %.3f cpu %.2f\n",
			st.floor >= 0 ? 1 : 0, ms, st.floor, st.floorWet, cpuMs);
	else
		printf("-hdwater: %d wet floor mirror draw(s), GPU %.4f ms (timestamps "
			"round the later copy and the mirror's draw) cpu %.2f\n",
			st.floor >= 0 ? 1 : 0, ms, cpuMs);
}

/* -marble 1: the engine's marble floors where HDFrameDrawGlow draws them,
 * before the wet floor's models, with Lara (-wetlara) or the stand-ins
 * (-wetstand) as the models. Drawn first with nothing laid over the frame
 * (HDRenderMarbleQuiet), the driver's first use of the clip plane and the
 * target timed apart, then for the picture, timed with a timestamp pair:
 * one marble: line. */
static void ProbeMarble(const void* projection)
{
	if (!g_marble_engine)
		return;

	const HDWetModelDraw draw = g_wet_lara_ready ? ProbeWetLaraDraw :
		!g_wet_stands.empty() ? ProbeWetStands : 0;
	MarbleTimer timer;

	HDRenderMarbleQuiet(true);
	timer.Begin();
	HDRenderMarbleDraw(draw, projection, 0);

	const double first = timer.End();

	HDRenderMarbleQuiet(false);
	timer.Begin();
	HDRenderMarbleDraw(draw, projection, 0);

	const double ms = timer.End();
	HDRenderMarbleStats s;

	HDRenderMarbleStatsNow(&s);
	printf("marble: planes %d drawn %d (cap %d) faces %d mirrored calls %d "
		"wet %d; level faces %d water %d opaque %d heights %d; GPU %.4f ms "
		"(timestamps round HDRenderMarbleDraw), first %.4f ms; spread %d "
		"rising %d; copies %d water %d buried %d joined %d\n", s.planes,
		s.drawn, g_marble_planes, s.faces, s.calls, s.wet, s.faces_level,
		s.water, s.opaque, s.heights, ms, first, s.spread, s.rising,
		s.copies, s.copies_water, s.buried, s.joined);

	// One marbleplane line a plane of the frame, in the order it is ranked.
	for (int i = 0; i < s.planes && i < 8; i++)
		printf("marbleplane: %d y %d cover %.5f %s room %d\n", i,
			s.plane_y[i], s.plane_cover[i], i < s.drawn ? "drawn" : "left",
			s.plane_room[i]);
}

/* The room behind the models' copy (HDRenderWaterWetRoomCopy) where
 * HDRenderWaterMarkBegin takes it, timed on the GPU with a timestamp pair:
 * a -hdwater line of its own, a whole frame's colour copied. */
static void ProbeWetRoomCopy()
{
	IDirect3DQuery9* q[3] = { 0, 0, 0 };
	IDirect3DQuery9* freq = 0;

	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &q[0]);
	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1]);
	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2]);
	g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &freq);

	const bool timed = q[0] && q[1] && q[2] && freq;

	if (timed)
	{
		q[0]->Issue(D3DISSUE_BEGIN);
		q[1]->Issue(D3DISSUE_END);
	}

	HDRenderWaterWetRoomCopy(true);

	double ms = -1.0;

	if (timed)
	{
		UINT64 a = 0, b = 0, f = 0;
		BOOL disjoint = TRUE;

		q[2]->Issue(D3DISSUE_END);
		q[0]->Issue(D3DISSUE_END);
		freq->Issue(D3DISSUE_END);

		while (q[0]->GetData(&disjoint, sizeof(disjoint), D3DGETDATA_FLUSH) ==
			S_FALSE)
			;

		while (q[1]->GetData(&a, sizeof(a), D3DGETDATA_FLUSH) == S_FALSE)
			;

		while (q[2]->GetData(&b, sizeof(b), D3DGETDATA_FLUSH) == S_FALSE)
			;

		while (freq->GetData(&f, sizeof(f), D3DGETDATA_FLUSH) == S_FALSE)
			;

		if (!disjoint && f)
			ms = (double)(b - a) * 1000.0 / (double)f;
	}

	for (int i = 0; i < 3; i++)
		if (q[i])
			q[i]->Release();

	if (freq)
		freq->Release();

	printf("-hdwater: wet room copy GPU %.4f ms (timestamps round the "
		"copy of the frame's colour before the models)\n", ms);
}

/* -wetagain: the wet floor's two draws once more after the frame's (its
 * darkening and its mirror twice in the picture), each timed as the first:
 * the first wet frame's draws against a second's. */
static bool g_wet_again = false;

/* The springs (hd_spring.h): with -hdwater 1 the level's springs, built as
 * the engine builds them after the sheets, ringing the sheets from the
 * frame's start and drawn where HDFrameDrawGlow draws them, after the
 * water's cards, timed on the GPU. -spring x,z,s adds one at world x, z of
 * strength s, 0 to 1 (repeatable); -nospring builds none - the picture as
 * the water drew it before the springs; -springparts 1 draws the columns
 * alone, 2 the bubbles alone, for their pixel counts. The SPRINGLOG lines
 * list the springs as the engine builds them; -springs is -hdwater 1, for
 * that list (with -nodraw, no picture), with a SPRINGRULE line for each
 * body; -springkey n builds them with the key hd_springs at n. Each spring
 * has its crack (hd_spring.cpp, "the crack"), drawn where HDFrameDrawGlow
 * draws it, before the copy; -springcrack 0 builds none, as the key
 * hd_springcrack at 0 does; -springs lists each crack and the points of
 * its outline (SPRINGCRACK lines), -springcrackmap draws the outline over
 * its body's squares as text. */
static bool g_spring_off = false;
static std::vector<float> g_spring_add;
/* -springs: also what the rule saw in each body (SPRINGRULE lines);
 * -springkey n: the key hd_springs as the engine would read it (1). */
static bool g_spring_list = false;
static int g_spring_key = 1;
// -springcrack n: the key hd_springcrack (1); -springcrackmap: the map.
static int g_spring_crack = 1;
static bool g_spring_crack_map = false;
// The picture's seed room, which the cracks' draw counts as drawn.
static int g_spring_seed_room = -1;

static void SpringBlocks(const char* stem, std::vector<HDSpringBlock>& out,
	std::vector<HDSpringRoom>* rooms);

// The springs' switches, apart from ProbeMain's chain of them.
static bool SpringArg(int argc, char** argv, int* at)
{
	if (!strcmp(argv[*at], "-nospring"))
	{
		g_spring_off = true;
		return true;
	}

	if (!strcmp(argv[*at], "-springs"))
	{
		g_hdwater = 1;
		g_spring_list = true;
		return true;
	}

	if (!strcmp(argv[*at], "-springkey") && *at + 1 < argc)
	{
		g_spring_key = atoi(argv[++*at]);
		return true;
	}

	if (!strcmp(argv[*at], "-springparts") && *at + 1 < argc)
	{
		HDSpringParts(atoi(argv[++*at]));
		return true;
	}

	if (!strcmp(argv[*at], "-springcrack") && *at + 1 < argc)
	{
		g_spring_crack = atoi(argv[++*at]) ? 1 : 0;
		return true;
	}

	if (!strcmp(argv[*at], "-springcrackmap"))
	{
		g_hdwater = 1;
		g_spring_crack_map = true;
		return true;
	}

	if (strcmp(argv[*at], "-spring") || *at + 1 >= argc)
		return false;

	float v[3];

	if (sscanf_s(argv[++*at], "%f,%f,%f", &v[0], &v[1], &v[2]) == 3 &&
		v[2] >= 0.0f && v[2] <= 1.0f)
		g_spring_add.insert(g_spring_add.end(), v, v + 3);
	else
		printf("RoomProbe: -spring wants x,z,strength (0..1) - '%s' "
			"ignored\n", argv[*at]);

	return true;
}

/* -springcrackmap: spring i's crack over its body's squares, a character
 * for each 16 units from 256 short of the outline's corners: '#'
 * inside the outline, '+' the spring's own point, '.' a square of the body
 * with deep water under it (hd_spring.h, kHDSpringDeep), '-' a shallower
 * one, ' ' none of the body's. */
static void SpringCrackMap(int i)
{
	const HDSpring* s = HDSpringAt(i);
	const HDSpringCrackShape* c = HDSpringCrackAt(i);

	if (!s || !c || s->body < 0)
		return;

	const int n = HDRenderWaterBodySquares(s->body, 0, 0, 0);
	std::vector<HDRenderWaterSquare> squares((size_t)(n > 0 ? n : 0));

	if (n > 0)
		HDRenderWaterBodySquares(s->body, &squares[0], n, 0);

	float x0 = 1.0e9f, z0 = 1.0e9f, x1 = -1.0e9f, z1 = -1.0e9f;

	for (int p = 0; p < c->points; p++)
	{
		x0 = fminf(x0, c->x[p]);
		z0 = fminf(z0, c->z[p]);
		x1 = fmaxf(x1, c->x[p]);
		z1 = fmaxf(z1, c->z[p]);
	}

	x0 = floorf(x0 / 256.0f) * 256.0f - 256.0f;
	z0 = floorf(z0 / 256.0f) * 256.0f - 256.0f;
	x1 = ceilf(x1 / 256.0f) * 256.0f + 256.0f;
	z1 = ceilf(z1 / 256.0f) * 256.0f + 256.0f;
	printf("SPRINGCRACKMAP spring=%d x %.0f..%.0f z %.0f..%.0f, 16 units a "
		"character, z down the lines\n", i, x0, x1, z0, z1);
	// And the body's squares the map meets: x,z,depth each.
	printf("SPRINGCRACKMAP spring=%d squares", i);

	for (const HDRenderWaterSquare& q : squares)
		if ((float)q.x + 1024.0f > x0 && (float)q.x < x1 &&
			(float)q.z + 1024.0f > z0 && (float)q.z < z1)
			printf(" %d,%d,%d", q.x, q.z, q.depth);

	printf("\n");

	for (float z = z0 + 8.0f; z < z1; z += 16.0f)
	{
		std::string row;

		for (float x = x0 + 8.0f; x < x1; x += 16.0f)
		{
			char ch = ' ';

			for (const HDRenderWaterSquare& q : squares)
				if (x >= (float)q.x && x < (float)(q.x + 1024) &&
					z >= (float)q.z && z < (float)(q.z + 1024))
					ch = q.depth >= kHDSpringDeep ? '.' : '-';

			if (HDSpringCrackHolds(*c, x, z))
				ch = '#';

			if (s->x >= x - 8.0f && s->x < x + 8.0f && s->z >= z - 8.0f &&
				s->z < z + 8.0f)
				ch = '+';

			row.push_back(ch);
		}

		printf("SPRINGCRACKMAP |%s|\n", row.c_str());
	}
}

static void SpringBuild(const char* root, const char* stem, int seed)
{
	HDSpringClear();
	g_spring_seed_room = seed;

	if (g_spring_off)
		return;

	std::vector<HDSpringBlock> blocks;
	std::vector<HDSpringRoom> rooms;

	SpringBlocks(stem, blocks, &rooms);
	HDSpringBuild(stem, blocks, g_spring_key);

	for (size_t i = 0; i + 2 < g_spring_add.size(); i += 3)
		if (!HDSpringAdd(stem, g_spring_add[i], g_spring_add[i + 1],
			g_spring_add[i + 2]))
			printf("-spring: no sheet over %.0f %.0f - no spring\n",
				g_spring_add[i], g_spring_add[i + 1]);

	// The cracks out of the installation's .TRG, as the engine reads it.
	const std::string trg = std::string(root) + "\\DATA\\" + stem + ".TRG";

	HDSpringCrackBuild(trg.c_str(), rooms, g_spring_crack);

	char line[256];

	for (int i = -1; HDSpringLogLine(i, line, sizeof(line)); i++)
		printf("%s\n", line);

	for (int i = 0; g_spring_list && HDSpringRuleLine(i, line, sizeof(line));
		i++)
		printf("%s\n", line);

	for (int i = 0; g_spring_list && i < HDSpringCount(); i++)
		for (int p = -1; HDSpringCrackLine(i, p, line, sizeof(line)); p++)
			printf("%s\n", line);

	for (int i = 0; g_spring_crack_map && i < HDSpringCount(); i++)
		SpringCrackMap(i);
}

static void SpringRings(double cx, double cy, double cz)
{
	if (g_hdwater == 1)
		HDSpringRings((float)(g_seed_origin[0] + cx), (float)cy,
			(float)(g_seed_origin[1] + cz));
}

static void SpringDraw()
{
	if (g_hdwater != 1 || !HDSpringCount())
		return;

	float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
		-g_seed_origin[0], 0, -g_seed_origin[1], 1 };
	double ms = -1.0;
	int columns = 0, bubbles = 0;

	HDSpringTiming(1, 0);
	HDSpringFrameDraw(m);
	HDSpringTiming(0, &ms);
	HDSpringDrawn(&columns, &bubbles);
	printf("-hdwater: %d spring draw(s) carrying %d column(s) and %d "
		"bubble(s), GPU %.4f ms (timestamps round the springs' draws)\n",
		(columns ? 1 : 0) + (bubbles ? 1 : 0), columns, bubbles, ms);
}

/* The frame's copy for the springs' columns, where HDFrameDrawGlow asks
 * for it beside the sheets' (hd_spring.cpp, HDSpringFrameCopy). */
static void SpringCopy()
{
	if (g_hdwater != 1 || !HDSpringCount())
		return;

	float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
		-g_seed_origin[0], 0, -g_seed_origin[1], 1 };

	HDSpringFrameCopy(m);
}

// Whether the picture draws a room: the seed's or one of -with's.
static bool SpringRoomDrawn(int room)
{
	if (room == g_spring_seed_room)
		return true;

	for (int i = 0; i < g_with_count; i++)
		if (g_with_group[i] == room)
			return true;

	return false;
}

/* The springs' cracks, where HDFrameDrawGlow draws them, timed on the GPU,
 * those whose floor's room the picture draws, as the engine's walk. */
static void SpringCrackDraw()
{
	if (g_hdwater != 1 || !HDSpringCount())
		return;

	float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
		-g_seed_origin[0], 0, -g_seed_origin[1], 1 };
	double ms = -1.0;
	int cracks = 0;

	HDSpringTiming(1, 0);
	HDSpringCrackFrameDraw(m, SpringRoomDrawn);
	HDSpringTiming(0, 0);
	HDSpringCrackNumbers(&cracks, &ms);
	printf("-hdwater: %d spring crack draw(s) carrying %d crack(s), GPU "
		"%.4f ms (timestamps round the cracks' draws)\n", cracks ? 1 : 0,
		cracks, ms);
}

/* The waterfall objects for their feet, out of the classic .TRC - the twin
 * of hd_level.cpp's BuildWaterFalls, which reads items[]: every item of
 * WATERFALL1 to 3 (slots 410 - 412) and WATERFALLSS1 and 2 (414, 415;
 * game\objects.h, ItemCheck names them), its one mesh placed as
 * DrawAnimatingItem places it - the item's place and turn through the
 * engine's own sine table, the first frame's root offset (the moveable's
 * frame pointer, the six shorts of the box first) - and of that mesh the
 * highest and the lowest y and the x, z of the vertices within
 * HD_WATER_FOOT_BAND of the lowest. A file mesh is its centre, its radius,
 * its vertex count and then its vertices, three shorts each. */
static void ProbeWaterFalls(const Tr5Level& level, const char* stem)
{
	std::vector<HDWaterFall> falls;
	const std::vector<uint8_t>& bytes = level.debugBytes();

	for (int i = 0; i < level.itemCount(); i++)
	{
		Tr5Item item;
		Tr5Level::MoveableInfo info;
		int16_t head[6];
		size_t at = 0;

		if (!level.getItem(i, item))
			continue;

		const int slot = item.objectNumber;

		if (!(slot >= 410 && slot <= 412) && !(slot >= 414 && slot <= 415))
			continue;

		if (!level.moveableInfo(slot, info) || info.nmeshes <= 0 ||
			!level.debugMeshHeader(info.meshIndex, head) ||
			!level.debugMeshAt(info.meshIndex, at) || head[5] <= 0 ||
			at + 12 + (size_t)head[5] * 6 > bytes.size())
			continue;

		const long word = (long)(info.frameOffset / 2);
		const float rootX = level.debugReadFrameShort(word + 6);
		const float rootY = level.debugReadFrameShort(word + 7);
		const float rootZ = level.debugReadFrameShort(word + 8);
		const float c = enginecam::PhdCos(item.yRot) * (1.0f / 16384.0f);
		const float s = enginecam::PhdSin(item.yRot) * (1.0f / 16384.0f);
		HDWaterFall f = {};

		f.item = i;
		f.slot = slot;
		f.room = item.roomNumber;
		f.top = 1.0e9f;
		f.bottom = -1.0e9f;

		for (int pass = 0; pass < 2; pass++)
			for (int v = 0; v < head[5]; v++)
			{
				int16_t p[3];

				std::memcpy(p, &bytes[at + 12 + (size_t)v * 6], 6);

				const float lx = p[0] + rootX;
				const float lz = p[2] + rootZ;
				const float y = (float)item.y + p[1] + rootY;

				if (!pass)
				{
					f.top = y < f.top ? y : f.top;
					f.bottom = y > f.bottom ? y : f.bottom;
					continue;
				}

				if (y < f.bottom - (float)HD_WATER_FOOT_BAND ||
					f.points >= HD_WATER_FOOT_POINTS)
					continue;

				f.foot[f.points * 2] = (float)item.x + lx * c + lz * s;
				f.foot[f.points * 2 + 1] = (float)item.z + lz * c - lx * s;
				f.points++;
			}

		falls.push_back(f);
	}

	HDRenderWaterFalls(falls.empty() ? 0 : &falls[0], (int)falls.size(),
		stem);

	/* The mist emitters (WATERFALLMIST, slot 368), the twin of hd_level.cpp's
	 * MoveWaterfallMist: the middle of each one's mist line and where the
	 * engine moves it, one WATERLOG line each, printed after the segments. */
	g_mist_lines.clear();

	for (int i = 0; i < level.itemCount(); i++)
	{
		Tr5Item item;

		if (!level.getItem(i, item) || item.objectNumber != 368)
			continue;

		const long ang = (long)item.yRot + 0x8000;
		const long side = ang ? ang - 0x4000 : ang + 0x4000;
		const long sa = (long)enginecam::PhdSin((short)ang);
		const long ca = (long)enginecam::PhdCos((short)ang);
		const long ss = (long)enginecam::PhdSin((short)side);
		const long cs = (long)enginecam::PhdCos((short)side);
		const long x = item.x - (512 * sa >> 14) + (256 * ss >> 14);
		const long z = item.z - (512 * ca >> 14) + (256 * cs >> 14);
		const float mx = (float)(x + (768 * sa >> 14));
		const float mz = (float)(z + (768 * ca >> 14));
		float nx = mx, nz = mz;

		if (!HDRenderWaterFootNearest(mx, mz, &nx, &nz))
			continue;

		char line[160];

		snprintf(line, sizeof(line), "WATERLOG level=%s mist=%d room=%d x=%d "
			"z=%d moved=%d,%d", stem, i, item.roomNumber, (int)mx, (int)mz,
			(int)floorf(nx - mx + 0.5f), (int)floorf(nz - mz + 0.5f));
		g_mist_lines.push_back(line);
	}
}

/* The level change. HDLevelBuild frees the previous level's buffers and
 * pages before it builds the new one - so the card never holds two levels at
 * once, and the process never commits two levels' worth of transient pixels.
 * This walks the same road without the game: build, measure, end, measure,
 * build the next. If the free were not happening, the free texture memory
 * after the second level would be lower than after the first by the whole
 * of the first. */
/* The HD pages of the classic water frames.
 *
 * The classic engine animates a water surface by rotating the contents of a
 * range of texinfos once every six game steps (specific/output.cpp,
 * S_AnimateTextures): the face keeps its texinfo number and the tile it shows
 * walks along the range. So the frames of one water surface are the tiles of
 * the range its texinfo belongs to, and the question this answers is what the
 * remaster paints each of those tiles with.
 *
 * `tiles` is the set of texinfo numbers the restored water faces name. Prints
 * one block per range that carries one of them and one summary line, which is
 * the same line the engine writes to HDLOG at level build.
 *
 * One thing the engine does that this does not, named rather than silently
 * absent: `BuildWaterFrames` skips the first `nAnimUVRanges` ranges while
 * `gfUVRotate` is non-zero, because those are the level's waterfalls - the
 * engine scrolls their v instead of rotating them, and a scrolled v walks
 * out of the cell the HD page paints. This tool has no gameflow and cannot
 * ask `gfUVRotate`, so it does not skip anything. It cannot bite on the
 * shipped game: `gfUVRotate` is the script's `UVROT` command, and in both
 * the vanilla `SCRIPT.DAT` and a randomized one it is 0 for every level with
 * a restored water face (ANDREA1, ANDREA2, ANDY1, ANDY3, JOBY2, JOBY4,
 * JOBY5) and non-zero only for TOM (3) and JOBY3 (8), neither of which
 * restores one. On a level where it were not 0, this tool would list a
 * range the engine drops.
 */
static void WaterFrames(const Tr5Level& level, const char* install,
	const char* stem, const std::vector<int>& tiles)
{
	size_t texAt = 0, animAt = 0;
	int texCount = 0, animShorts = 0;

	if (!level.debugTexinfoBlock(texAt, texCount) ||
		!level.debugAnimTexBlock(animAt, animShorts))
	{
		printf("  no texinfo or animated texture block in %s\n", stem);
		return;
	}

	const std::vector<unsigned char>& raw = level.debugBytes();
	std::vector<unsigned short> hd((size_t)texCount, 0xFFFFu);
	const int got = HDScanLevelTextures(install, stem, &hd[0], texCount);

	printf("\n-- the water frames -----------------------------------------\n");
	printf("  %s: %d texinfo(s), the .TEX names %d of them\n",
		stem, texCount, got);

	/* The per-cell rule, checked on this level rather than believed. TRForge
	 * measured over the shipped games that every tile with HD art lies wholly
	 * inside one 64x64 cell and that no cell ever wants two different files;
	 * the substitution rests on both, so both are counted here. */
	{
		std::vector<int> key, page;
		int crossing = 0, conflict = 0, withArt = 0;

		for (int t = 0; t < texCount && t < got; t++)
		{
			if (hd[(size_t)t] == 0xFFFF)
				continue;

			const size_t o = texAt + (size_t)t * 40;
			unsigned short tp = 0;
			int u[4], v[4];

			memcpy(&tp, &raw[o + 2], 2);

			for (int c = 0; c < 4; c++)
			{
				unsigned short a = 0, b = 0;

				memcpy(&a, &raw[o + 6 + (size_t)c * 4], 2);
				memcpy(&b, &raw[o + 8 + (size_t)c * 4], 2);
				u[c] = a >> 8;
				v[c] = b >> 8;
			}

			/* Triangle or quad, and which word says so. In the file the bit
			 * sits at the top of the TileAndFlag word (+2, the one this reads
			 * as `tp`), not at the top of the NewFlags word (+4, `fl`); the
			 * runtime TEXTURESTRUCT is the other way round, because
			 * LoadTextureInfos builds its `flag` as the low 15 bits of the
			 * file's flag word with bit 15 of the file's tpage word on top
			 * (`tex.tpage ^ (tex.tpage ^ tex.flag) & 0x7FFF`,
			 * specific/file.cpp), and AdjustUV then tests `flag & 0x8000`. So
			 * this line has to read `tp`, matching the engine. The two bits
			 * disagree often - JOBY2 has 1,052 tiles with the one and 400
			 * with the other - and the counts below come out the same either
			 * way, but a proof tool has no business being accidentally
			 * right.
			 *
			 * A triangle's fourth corner is not a corner at all: AdjustUV
			 * touches three of them and the draw reads three. Counting the
			 * fourth turns 127 of JOBY2's 382 tiles into cell crossers that
			 * are nothing of the kind. */
			const int corners = (tp & 0x8000) ? 3 : 4;
			int u0 = u[0], u1 = u[0], v0 = v[0], v1 = v[0];

			for (int c = 1; c < corners; c++)
			{
				if (u[c] < u0) u0 = u[c];
				if (u[c] > u1) u1 = u[c];
				if (v[c] < v0) v0 = v[c];
				if (v[c] > v1) v1 = v[c];
			}

			withArt++;

			if (u0 / 64 != (u1 - 1) / 64 || v0 / 64 != (v1 - 1) / 64)
			{
				crossing++;
				continue;
			}

			const int k = ((tp & 0x7FFF) << 4) | ((v0 / 64) << 2) | (u0 / 64);
			bool seen = false;

			for (size_t i = 0; i < key.size(); i++)
				if (key[i] == k)
				{
					seen = true;

					if (page[i] != (int)hd[(size_t)t])
						conflict++;
				}

			if (!seen)
			{
				key.push_back(k);
				page.push_back((int)hd[(size_t)t]);
			}
		}

		printf("  the cell rule: %d tile(s) with HD art in %d cell(s), "
			"%d crossing a cell edge, %d cell(s) wanting two files\n",
			withArt, (int)key.size(), crossing, conflict);
	}

	if (!got)
		return;

	// One short: the number of ranges. Then per range one short with the
	// number of frames MINUS ONE, and that many plus one texinfo numbers.
	std::vector<short> anim((size_t)animShorts);

	for (int i = 0; i < animShorts; i++)
	{
		short v = 0;
		memcpy(&v, &raw[animAt + (size_t)i * 2], 2);
		anim[(size_t)i] = v;
	}

	int at = 0;
	const int ranges = animShorts ? anim[0] : 0;
	int waterRanges = 0, waterFrames = 0, missing = 0;
	std::vector<int> pages;

	at = 1;

	for (int r = 0; r < ranges && at < animShorts; r++)
	{
		const int frames = anim[(size_t)at] + 1;	// the count is n - 1
		const int first = at + 1;

		at = first + frames;

		if (at > animShorts)
			break;

		bool wanted = false;

		for (int f = 0; f < frames && !wanted; f++)
			for (size_t w = 0; w < tiles.size(); w++)
				if (tiles[w] == anim[(size_t)(first + f)])
				{
					wanted = true;
					break;
				}

		if (!wanted)
			continue;

		waterRanges++;
		printf("  range %2d: %d frame(s)\n", r, frames);

		// The HD page of each frame, in the range's own order, for the
		// rotation below.
		std::vector<int> framePage((size_t)frames, -1);

		for (int f = 0; f < frames; f++)
		{
			const int tile = anim[(size_t)(first + f)];

			if (tile < 0 || tile >= texCount)
				continue;

			const size_t o = texAt + (size_t)tile * 40;
			unsigned short tp = 0;
			int tu[4], tv[4];

			memcpy(&tp, &raw[o + 2], 2);

			for (int c = 0; c < 4; c++)
			{
				unsigned short a = 0, b = 0;

				memcpy(&a, &raw[o + 6 + (size_t)c * 4], 2);
				memcpy(&b, &raw[o + 8 + (size_t)c * 4], 2);
				tu[c] = a >> 8;
				tv[c] = b >> 8;
			}

			// Bit 15 of TileAndFlag (+2), not of NewFlags (+4) - see the note
			// on the same line in the cell rule above.
			const int corners = (tp & 0x8000) ? 3 : 4;
			int ru0 = tu[0], ru1 = tu[0], rv0 = tv[0], rv1 = tv[0];

			for (int c = 1; c < corners; c++)
			{
				if (tu[c] < ru0) ru0 = tu[c];
				if (tu[c] > ru1) ru1 = tu[c];
				if (tv[c] < rv0) rv0 = tv[c];
				if (tv[c] > rv1) rv1 = tv[c];
			}

			const unsigned short u1 = (unsigned short)(ru0 << 8);
			const unsigned short v1 = (unsigned short)(rv0 << 8);

			const int px = (u1 >> 8), py = (v1 >> 8);
			const int page = hd[(size_t)tile];

			waterFrames++;

			if (page == 0xFFFF)
				missing++;
			else
			{
				bool seen = false;

				for (size_t i = 0; i < pages.size(); i++)
					if (pages[i] == page)
						seen = true;

				if (!seen)
					pages.push_back(page);
			}

			/* The rectangle the tile occupies inside its HD page: the cell
			 * is painted at eight times the size, so a classic pixel at
			 * (px % 64) sits at eight times that in the 512 file. This is
			 * the same arithmetic HDWaterFrameFor does in floats. */
			printf("    frame %d: tile %4d on page %3d at (%3d,%3d)..(%3d,%3d), "
				"cell (%d,%d)  ->  %s%d, HD rect (%d,%d) %dx%d\n",
				f, tile, tp & 0x7FFF, ru0, rv0, ru1, rv1, px / 64, py / 64,
				page == 0xFFFF ? "no HD page for tile " : "", page,
				(ru0 % 64) * 8, (rv0 % 64) * 8,
				(ru1 - ru0 + 1) * 8, (rv1 - rv0 + 1) * 8);
			framePage[(size_t)f] = page;
		}

		/* And the runtime half, run rather than believed. The engine does not
		 * know which frame a texinfo is showing: it matches the CONTENT that
		 * is in it right now against the frames it recorded at load
		 * (hd_level.cpp, HDWaterFrameFor). So the classic rotation is
		 * simulated here - textinfo[range[0]] = textinfo[range[1]] all the
		 * way along and the first one round to the end, which is
		 * S_AnimateTextures' own loop - and after every step the frame that
		 * the range's FIRST texinfo now holds is read off. It has to walk the
		 * range in order and come back round after `frames` steps. */
		{
			std::vector<int> content((size_t)frames);
			bool ok = true;

			for (int i = 0; i < frames; i++)
				content[(size_t)i] = i;

			printf("      the rotation: ");

			for (int step = 0; step < frames * 2 + 1; step++)
			{
				const int show = content[0];
				const int want = step % frames;

				printf("%s%d", step ? " " : "", framePage[(size_t)show]);

				if (show != want)
					ok = false;

				const int keep = content[0];

				for (int i = 0; i + 1 < frames; i++)
					content[(size_t)i] = content[(size_t)(i + 1)];

				content[(size_t)(frames - 1)] = keep;
			}

			printf("  ->  %s\n", ok ? "walks the range and comes back round"
				: "OUT OF STEP");
		}
	}

	std::string list;

	for (size_t i = 0; i < pages.size(); i++)
	{
		char one[32];

		sprintf(one, "%s%d", i ? "," : "", pages[i]);
		list += one;
	}

	printf("  HDLOG level=%s water ranges=%d frames=%d hd pages=%s missing=%d\n",
		stem, waterRanges, waterFrames,
		list.empty() ? "none" : list.c_str(), missing);
}

static void RunSequence(const char* root, const char* list, int rooms,
	bool managed, int threads)
{
	char names[512];
	const char* why = "";

	strncpy(names, list, sizeof(names) - 1);
	names[sizeof(names) - 1] = 0;

	printf("\n-- the level change -----------------------------------------\n");
	printf("%-10s %10s %10s %10s %10s %10s\n", "level", "pages MiB",
		"vb+ib MiB", "commit", "peak", "free vram");

	char* save = 0;
	char* one = strtok_s(names, ",;", &save);

	while (one)
	{
		while (*one == ' ')
			one++;

		HDRenderStats stats;

		if (!HDRenderLevelBegin(root, one, rooms, managed, threads, &stats, &why))
		{
			printf("%-10s FAILED - %s\n", one, why);
			HDRenderLevelEnd();
			one = strtok_s(0, ",;", &save);
			continue;
		}

		printf("%-10s %10.1f %10.1f %10.1f %10.1f %10.1f\n", one,
			stats.pages.bytes / 1048576.0,
			(stats.vertexBytes + stats.indexBytes) / 1048576.0,
			CommitMiB(), PeakCommitMiB(),
			(double)g_device->GetAvailableTextureMem() / 1048576.0);

		HDRenderLevelEnd();

		printf("%-10s %10s %10s %10.1f %10.1f %10.1f\n", "  (freed)", "-", "-",
			CommitMiB(), PeakCommitMiB(),
			(double)g_device->GetAvailableTextureMem() / 1048576.0);

		one = strtok_s(0, ",;", &save);
	}

	printf("\nA level's own numbers must repeat when it comes round again: if\n"
		"the free texture memory after the second TITLE is lower than after\n"
		"the first, something of the level in between is still on the card.\n");
}

/* ---------------------------------------------------------------- main -- */

/* Whether the engine draws group g in HD: hd_level.cpp's rule, out of the
 * same build - triangles, not all of them overhang, not named whole. */
static bool StandGroupHD(int g)
{
	const int tris = HDRenderGroupTriangles(g, -1);

	return tris > 0 && HDRenderGroupOverhangTriangles(g) < tris &&
		!(g_hdm && HDMarkersRoomTouched(g));
}

/* A holder's verdict as hd_level.cpp's BuildStandRanges reaches it: per HD
 * group the renderer's coverage of the holder's faces by its tagged
 * triangles (HDRenderStandCoverAt), the carrier the one that covers the most
 * faces (HDCoverCarrierBetter) and the room's state (HDCoverRoomState).
 * Printed once; `carrier` gets the carrier or -1 and `covered` its flag per
 * face, in the order of the holder's faces. The guards of BuildStandRanges -
 * ROOM_MIST or ROOM_REFLECT, a carrier with a flipmap twin - are not asked:
 * no room of the game the art covers a face of meets them (TrgProbe
 * -classicart -perface, `guard=`). */
static int StandState(int h, int* carrier = 0, const unsigned char** covered = 0)
{
	for (size_t i = 0; i < g_stand_covered_room.size(); i++)
		if (g_stand_covered_room[i] == h)
		{
			if (carrier)
				*carrier = g_stand_carrier[i];

			if (covered)
				*covered = g_stand_flags[i];

			return g_stand_covered_answer[i];
		}

	const HDStandHolder* holder = 0;

	for (const HDStandHolder& one : g_stand_holders_built)
		if (one.room == h)
			holder = &one;

	int tagged = 0;

	for (int i = 0; i < HDRenderStandTriangleCount(); i++)
	{
		HDCoverTri tri;
		int at = -1;

		if (HDRenderStandTriangleAt(i, &tri, &at) && at == h &&
			StandGroupHD(tri.group))
			tagged++;
	}

	int best = -1, bestFaces = 0, bestTris = 0, faces = 0;
	const unsigned char* flags = 0;

	if (holder && tagged && !StandGroupHD(h))
	{
		if (!g_stand_quiet)
			printf("stand: room %d holds %d triangle(s) of stand art\n", h, tagged);

		for (int c = 0; c < HDRenderStandCovers(); c++)
		{
			HDStandCover cover;

			if (!HDRenderStandCoverAt(c, &cover) || cover.holder != h ||
				!StandGroupHD(cover.group) || cover.faceCount != holder->faceCount ||
				!cover.covered)
				continue;

			faces = cover.faces;
			if (!g_stand_quiet)
				printf("stand: room %d group %d %d triangle(s) cover faces %d/%d\n", h,
					cover.group, cover.tris, cover.facesCovered, cover.faces);

			if (HDCoverCarrierBetter(cover.facesCovered, cover.tris, cover.group,
				bestFaces, bestTris, best))
			{
				best = cover.group;
				bestFaces = cover.facesCovered;
				bestTris = cover.tris;
				flags = cover.covered;
			}
		}

		const int state = best >= 0 ? HDCoverRoomState(faces, bestFaces) :
			HD_STAND_ROOM_CLASSIC;

		if (!g_stand_quiet)
			printf("stand: room %d - %s\n", h, state == HD_STAND_ROOM_COVERED ?
				"COVERED, drawn as the carrier's art" : state == HD_STAND_ROOM_PART ?
				"PART, the covered faces drawn as the carrier's art" :
				"not covered, drawn classic");

		if (best >= 0 && !g_stand_quiet)
			printf("stand: room %d carrier %d faces %d/%d\n", h, best, bestFaces, faces);

		if (state == HD_STAND_ROOM_CLASSIC)
		{
			best = -1;
			flags = 0;
		}

		g_stand_covered_room.push_back(h);
		g_stand_covered_answer.push_back(state);
		g_stand_carrier.push_back(best);
		g_stand_flags.push_back(flags);

		if (carrier)
			*carrier = best;

		if (covered)
			*covered = flags;

		return state;
	}

	if (!g_stand_quiet)
		printf("stand: room %d has no stand art%s - drawn as before\n", h,
			StandGroupHD(h) ? " (its own group is drawn HD)" : "");

	g_stand_covered_room.push_back(h);
	g_stand_covered_answer.push_back(HD_STAND_ROOM_CLASSIC);
	g_stand_carrier.push_back(-1);
	g_stand_flags.push_back(0);

	if (carrier)
		*carrier = -1;

	if (covered)
		*covered = 0;

	return HD_STAND_ROOM_CLASSIC;
}

static bool StandCovered(int h, int* carrier = 0)
{
	return StandState(h, carrier) == HD_STAND_ROOM_COVERED;
}

/* -stand: every stand range of every group answered as DecideStand
 * (hd_level.cpp) answers it, with the -classicroom rooms as the listed
 * holders and the seed and the -with groups, less -skipgroup, as the listed
 * art; the in-holder draws get their group's offset from the seed. */
static void PrepareStand(int seed)
{
	g_stand_draws.clear();

	if (!g_stand_rule)
		return;

	const auto listed = [&](int g)
	{
		if (g_skip_groups.count(g))
			return false;

		if (g == seed)
			return true;

		for (int i = 0; i < g_with_count; i++)
			if (g_with_group[i] == g)
				return true;

		return false;
	};

	int with = 0, inHolder = 0, out = 0;

	for (int g = 0; g < HDRenderGroupCount(); g++)
	{
		if (!StandGroupHD(g))
			continue;

		for (int j = 0; j < HDRenderGroupStandRanges(g); j++)
		{
			HDStandRange range;

			if (!HDRenderGroupStandRange(g, j, &range))
				continue;

			bool holderListed = false;

			for (int h : g_classic_rooms)
				holderListed = holderListed || h == range.holder;

			// Only a holder the frame would ask about: its own group not HD.
			int carrier = -1;
			const int state = holderListed ? StandState(range.holder, &carrier) :
				HD_STAND_ROOM_CLASSIC;
			const bool covered = state == HD_STAND_ROOM_COVERED;
			const HDStandAnswer answer = HDStandDrawFaces(holderListed, listed(g),
				state, carrier == g, range.kind, range.faces);

			HDRenderStandWith(g, j, answer.behind && answer.withArt ? HD_STAND_BEHIND
				: answer.withArt ? HD_STAND_WITH : HD_STAND_OUT);

			if (answer.withArt)
				with++;
			else if (!answer.inHolder && listed(g))
				out++;

			if (holderListed)
				printf("stand: group %d range %d holder %d kind %d tris %d "
					"listed %d covered %d -> %s faces %d state %d\n", g, j,
					range.holder, range.kind, range.triangles, listed(g) ? 1 : 0,
					covered ? 1 : 0, answer.behind && answer.withArt ?
					"with its group, pushed back" : answer.withArt ? "with its group"
					: answer.inHolder && answer.behind ?
					"in the holder's window, pushed back" : answer.inHolder ?
					"in the holder's window" : "left out", range.faces, state);

			if (!answer.inHolder || g >= (int)g_stand_rooms_built.size() ||
				seed < 0 || seed >= (int)g_stand_rooms_built.size())
				continue;

			StandDrawProbe draw;

			memset(&draw.world, 0, sizeof(draw.world));
			draw.world._11 = draw.world._22 = draw.world._33 = draw.world._44 = 1.0f;
			draw.world._41 = g_stand_rooms_built[(size_t)g].origin[0] -
				g_stand_rooms_built[(size_t)seed].origin[0];
			draw.world._42 = g_stand_rooms_built[(size_t)g].origin[1] -
				g_stand_rooms_built[(size_t)seed].origin[1];
			draw.world._43 = g_stand_rooms_built[(size_t)g].origin[2] -
				g_stand_rooms_built[(size_t)seed].origin[2];
			draw.group = g;
			draw.index = j;
			draw.behind = answer.behind;
			g_stand_draws.push_back(draw);
			inHolder++;
		}
	}

	printf("stand: %d range(s) with their group, %d in the holder's window, "
		"%d left out\n", with, inHolder, out);
}

/* -standreceive: the projected shadow's receive of every range drawn in its
 * holder's window, as hd_level.cpp's ReceiveStandInHolders draws it after
 * the solid and the clear phase (HDRenderRoomShadowStand), counted with an
 * occlusion query: the pixels the receive reaches at the depth the range was
 * drawn at, and - a receive of its own each, so the stencil guard starts
 * clean - the pixels it would reach with the other depth push. The receive
 * tests depth EQUAL, so a range drawn pushed back is reached only pushed
 * back and one drawn in place only in place: the first count is the range's
 * visible pixels, the second 0. The map is made and left as it is; the
 * count does not depend on what it holds. */
static bool g_stand_receive = false;

static void StandReceiveCount(const HDRoomLighting* lighting)
{
	if (!g_stand_receive)
		return;

	if (!HDRenderShadowReceiveReady() || !HDRenderShadowTargetReady())
	{
		printf("standreceive: no receive on this device\n");
		return;
	}

	float regs[64 * 4];

	memset(regs, 0, sizeof(regs));

	long total[2] = { 0, 0 };

	for (const StandDrawProbe& draw : g_stand_draws)
	{
		long count[2] = { -1, -1 };

		for (int flip = 0; flip < 2; flip++)
		{
			IDirect3DQuery9* query = 0;
			DWORD pixels = 0;

			if (!HDRenderShadowReceiveBegin(regs))
				continue;

			if (SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query)))
			{
				query->Issue(D3DISSUE_BEGIN);
				HDRenderRoomShadowStand(draw.group, &draw.world, lighting, draw.index,
					flip ? !draw.behind : draw.behind);
				query->Issue(D3DISSUE_END);

				while (query->GetData(&pixels, sizeof(pixels), D3DGETDATA_FLUSH) == S_FALSE)
					;

				query->Release();
				count[flip] = (long)pixels;
			}

			HDRenderShadowReceiveEnd();
		}

		printf("standreceive: group %d range %d %s - %ld pixel(s) at its own depth, "
			"%ld at the other push\n", draw.group, draw.index,
			draw.behind ? "pushed back" : "in place", count[0], count[1]);

		for (int k = 0; k < 2; k++)
			if (count[k] > 0)
				total[k] += count[k];
	}

	printf("standreceive: %d range(s) in a holder's window, %ld pixel(s) reached at "
		"their own depth, %ld at the other push\n", (int)g_stand_draws.size(), total[0],
		total[1]);
}

static void DrawStandInHolders(const HDRoomLighting* lighting, unsigned int passes)
{
	for (const StandDrawProbe& draw : g_stand_draws)
		HDRenderRoomStand(draw.group, &draw.world, lighting, passes, draw.index,
			draw.behind);
}

/* The static meshes - -statictable, -statics, -static and -staticsonly.
 *
 * The engine answers once per level which statics the art draws
 * (hd_level.cpp, BuildStaticTables, through HDRenderStaticArt and the rule in
 * hd_cover.h) and DrawStaticObjects asks the answer per frame. This tool has
 * the same renderer, so it asks the same function, with the groups' states
 * out of StandState: -statictable prints the answer of every static of the
 * level beside the answer the rule before it gave (only the room's own group
 * asked, and only in a room drawn HD), the distribution of the shares, and
 * every shatterable static with the pieces the engine pairs it with.
 *
 * -statics new|old draws the classic statics a frame of this picture draws -
 * those of the listed rooms (the seed, the -with groups and the
 * -classicroom rooms) that the rule leaves to the classic path, the frame
 * listing the seed and the -with groups - flat like the classic faces; new
 * is this engine's rule, old the one before it. -static <room>:<mesh> draws
 * that static whatever a rule says (repeatable); -staticsonly draws the
 * statics alone over a cleared picture. With -coincide each set of statics
 * gets a line of its own: the pixels where a static lands within the units
 * of the art (`within`), and those where it stands in front of the art by
 * more than the units (`beyond` - the art has nothing for it there). The
 * mesh is the static's own (not its far copy), both sides drawn. */
namespace probestatics
{
	struct Static
	{
		int room;
		int mesh;
		long x, y, z;
		short rot;
		short flags;
		short slot;
		float mins[3], maxs[3];
		HDStaticArt art;
		int oldAnswer;
	};

	static std::vector<Static> g_list;
	static Tr5Level g_level;
	static bool g_loaded = false;
	static short g_vis[70][6];
	static int g_mesh_of[70];
	static bool g_have[70];
	static int g_rule = 0;			// -statics: 0 none, 1 new, 2 old
	static std::vector<std::pair<int, int> > g_forced;	// -static
	static bool g_only = false;		// -staticsonly
	static bool g_table = false;		// -statictable
	static bool g_answered = false;

	/* The flipmap twin of a room as BuildStaticTables takes it: the main
	 * room names it, the alternate is the room that names it; -1 for none.
	 * The pairs leave the twin's pieces (HDStaticPieceInState). */
	static int Twin(int r)
	{
		const std::vector<Tr5Room>& rooms = g_level.rooms();

		if (r < 0 || r >= (int)rooms.size())
			return -1;

		if (rooms[(size_t)r].altRoom >= 0)
			return rooms[(size_t)r].altRoom;

		for (size_t m = 0; m < rooms.size(); m++)
			if (rooms[m].altRoom == r)
				return (int)m;

		return -1;
	}

	static bool Load(const char* stem)
	{
		if (g_loaded)
			return true;

		if (g_roombox.empty())
			return false;

		std::string error;
		const std::string trc = g_roombox + "\\" + stem + ".TRC";
		std::wstring wide;

		for (size_t i = 0; i < trc.size(); i++)
			wide.push_back((wchar_t)(unsigned char)trc[i]);

		if (!g_level.load(wide, error))
		{
			printf("statics: %s could not be read (%s)\n", trc.c_str(), error.c_str());
			return false;
		}

		memset(g_vis, 0, sizeof(g_vis));
		memset(g_have, 0, sizeof(g_have));

		/* The level's static block follows the moveables: a count, then 32
		 * bytes per slot - the slot number, the mesh, the visibility box
		 * (STATIC_INFO x_minp..z_maxp) and the collision box. */
		size_t objAt = 0, frAt = 0;
		int32_t objN = 0, frN = 0;

		if (g_level.debugModelBlocks(objAt, objN, frAt, frN))
		{
			const std::vector<uint8_t>& raw = g_level.debugBytes();
			size_t at = objAt + (size_t)objN * 20;
			int n = 0;

			memcpy(&n, raw.data() + at, 4);
			at += 4;

			for (int i = 0; i < n; i++, at += 32)
			{
				int slot = 0;
				unsigned short mesh = 0;

				memcpy(&slot, raw.data() + at, 4);
				memcpy(&mesh, raw.data() + at + 4, 2);

				if (slot < 0 || slot >= 70)
					continue;

				memcpy(g_vis[slot], raw.data() + at + 6, 12);
				g_mesh_of[slot] = mesh;
				g_have[slot] = true;
			}
		}

		/* Every room's MESH_INFO list, 20 bytes each: x, y, z, y_rot, shade,
		 * Flags, static_number. The box as hd_level.cpp's StaticBox makes it:
		 * the visibility box's four XZ corners turned through the engine's
		 * sine table, room local. */
		const std::vector<uint8_t>& raw = g_level.debugBytes();

		for (int r = 0; r < g_level.roomCount(); r++)
		{
			const Tr5Room& rm = g_level.rooms()[(size_t)r];
			unsigned meshOfs = 0, lightsMeshes = 0, yOrigin = 0;

			if (!rm.headerOffset || !g_level.headerField(r, 16, meshOfs, error) ||
				!g_level.headerField(r, 48, lightsMeshes, error))
				continue;

			g_level.headerField(r, 24, yOrigin, error);

			const int count = (int)(lightsMeshes >> 16);

			for (int m = 0; m < count; m++)
			{
				const size_t at = rm.headerOffset + 208 + meshOfs + (size_t)m * 20;

				if (at + 20 > raw.size())
					break;

				Static s;
				int mx = 0, my = 0, mz = 0;

				memset(&s, 0, sizeof(s));
				memcpy(&mx, raw.data() + at, 4);
				memcpy(&my, raw.data() + at + 4, 4);
				memcpy(&mz, raw.data() + at + 8, 4);
				memcpy(&s.rot, raw.data() + at + 12, 2);
				memcpy(&s.flags, raw.data() + at + 16, 2);
				memcpy(&s.slot, raw.data() + at + 18, 2);
				s.room = r;
				s.mesh = m;
				s.x = mx;
				s.y = my;
				s.z = mz;

				if (s.slot < 0 || s.slot >= 70)
					continue;

				const short* v = g_vis[s.slot];
				const float c = (float)enginecam::PhdCos(s.rot) * (1.0f / 16384.0f);
				const float sn = (float)enginecam::PhdSin(s.rot) * (1.0f / 16384.0f);
				const float xs[4] = { (float)v[0], (float)v[1], (float)v[0], (float)v[1] };
				const float zs[4] = { (float)v[4], (float)v[4], (float)v[5], (float)v[5] };
				const float cx = (float)(mx - rm.originX);
				const float cz = (float)(mz - rm.originZ);

				for (int k = 0; k < 4; k++)
				{
					const float wx = cx + xs[k] * c + zs[k] * sn;
					const float wz = cz + zs[k] * c - xs[k] * sn;

					if (!k)
					{
						s.mins[0] = s.maxs[0] = wx;
						s.mins[2] = s.maxs[2] = wz;
					}
					else
					{
						if (wx < s.mins[0]) s.mins[0] = wx;
						if (wx > s.maxs[0]) s.maxs[0] = wx;
						if (wz < s.mins[2]) s.mins[2] = wz;
						if (wz > s.maxs[2]) s.maxs[2] = wz;
					}
				}

				s.mins[1] = (float)(my - (int)yOrigin + v[2]);
				s.maxs[1] = (float)(my - (int)yOrigin + v[3]);
				g_list.push_back(s);
			}
		}

		g_loaded = true;
		return true;
	}

	/* A static's own mesh vertices where they stand, local to its room as
	 * BuildStaticTables makes them out of MESH_DATA::aVtx: x and z from the
	 * room's origin, y less the room's y. The vertex count is the low byte
	 * of the word at +10, as the engine's mesh conversion reads it. */
	static void Verts(const Static& s, std::vector<float>& out)
	{
		out.clear();

		size_t at = 0;
		const std::vector<uint8_t>& raw = g_level.debugBytes();

		if (!g_have[s.slot] || !g_level.debugMeshAt(g_mesh_of[s.slot], at) ||
			at + 12 > raw.size() || s.room >= (int)g_stand_rooms_built.size())
			return;

		short word = 0;

		memcpy(&word, raw.data() + at + 10, 2);

		const int verts = word & 0xFF;
		const float c = (float)enginecam::PhdCos(s.rot) * (1.0f / 16384.0f);
		const float sn = (float)enginecam::PhdSin(s.rot) * (1.0f / 16384.0f);
		const float* o = g_stand_rooms_built[(size_t)s.room].origin;

		for (int v = 0; v < verts; v++)
		{
			short p[3] = { 0, 0, 0 };
			const size_t q = at + 12 + (size_t)v * 6;

			if (q + 6 > raw.size())
				break;

			memcpy(p, raw.data() + q, 6);

			const float x = (float)p[0], y = (float)p[1], z = (float)p[2];

			out.push_back((float)(s.x - (long)o[0]) + x * c + z * sn);
			out.push_back((float)(s.y - (long)o[1]) + y);
			out.push_back((float)(s.z - (long)o[2]) + z * c - x * sn);
		}
	}

	/* The places of the statics, handed to the build as hd_level.cpp's
	 * BuildStaticPlaces hands them - the boxes moved to world x and z - in
	 * the order of g_list, which is the place each static is asked with. */
	static std::vector<HDStaticPlace> g_places;

	static void HandPlaces(const char* stem)
	{
		if (!Load(stem))
			return;

		g_places.clear();

		for (const Static& s : g_list)
		{
			HDStaticPlace place;
			const Tr5Room& rm = g_level.rooms()[(size_t)s.room];

			place.room = s.room;

			for (int axis = 0; axis < 3; axis++)
			{
				place.mins[axis] = s.mins[axis];
				place.maxs[axis] = s.maxs[axis];
			}

			place.mins[0] += (float)rm.originX;
			place.maxs[0] += (float)rm.originX;
			place.mins[2] += (float)rm.originZ;
			place.maxs[2] += (float)rm.originZ;
			g_places.push_back(place);
		}

		HDRenderSetStaticPlaces(g_places.empty() ? 0 : &g_places[0],
			(int)g_places.size());
	}

	/* The answers, as BuildStaticTables gets them: per group drawn HD or
	 * not (StandGroupHD), the state and carrier of a room drawn classic
	 * (StandState), the room's origin. */
	static void Answer(const char* stem)
	{
		if (g_answered || !Load(stem) || g_stand_rooms_built.empty())
			return;

		g_answered = true;

		const int groups = (int)g_stand_rooms_built.size();
		std::vector<HDStaticGroup> asked((size_t)groups);

		g_stand_quiet = true;

		for (int g = 0; g < groups; g++)
		{
			HDStaticGroup& one = asked[(size_t)g];
			int carrier = -1;

			one.hd = StandGroupHD(g) ? 1 : 0;
			one.state = (unsigned char)(one.hd ? HD_STAND_ROOM_CLASSIC :
				StandState(g, &carrier));
			one.carrier = (short)(one.state != HD_STAND_ROOM_CLASSIC ? carrier : -1);
			one.originX = g_stand_rooms_built[(size_t)g].origin[0];
			one.originZ = g_stand_rooms_built[(size_t)g].origin[2];
		}

		g_stand_quiet = false;

		std::vector<float> verts;

		for (size_t at = 0; at < g_list.size(); at++)
		{
			Static& s = g_list[at];

			Verts(s, verts);
			HDRenderStaticArt(s.room, s.mins, s.maxs, verts.empty() ? 0 : &verts[0],
				(int)verts.size() / 3, &asked[0], groups,
				at < g_places.size() ? (int)at : -1, &s.art);

			const bool hd = s.room < groups && asked[(size_t)s.room].hd;

			s.oldAnswer = hd && HDStaticCellsHold(s.art.own, s.art.boxCells) ?
				HD_STATIC_ART : HD_STATIC_CLASSIC;
		}

		// A room's pieces all or none, and their index buffer, as
		// BuildStaticTables ends.
		std::vector<int> rooms;
		std::vector<HDStaticArt> arts;

		for (const Static& s : g_list)
		{
			rooms.push_back(s.room);
			arts.push_back(s.art);
		}

		if (!arts.empty())
			HDRenderStaticPiecesWhole(&rooms[0], &arts[0], (int)arts.size());

		for (size_t at = 0; at < g_list.size(); at++)
			g_list[at].art = arts[at];

		HDRenderStaticPiecesUpload();
		HDRenderSetStaticPlaces(0, 0);
	}

	/* The pieces this picture draws (hd_cover.h, HDStaticDrawSource): of the
	 * statics of a listed room held by groups none of which the picture lists,
	 * each one with a piece, at its group's offset from the seed, over the
	 * whole screen like every group of the picture. */
	struct PieceDrawProbe
	{
		int piece;
		int room;
		int mesh;
		D3DMATRIX world;
	};

	static std::vector<PieceDrawProbe> g_piece_draws;
	static std::vector<std::pair<int, int> > g_piece_only;	// -piece
	static std::vector<int> g_list_rooms;			// -listroom

	static bool PieceDrawn(const Static& s)
	{
		for (const PieceDrawProbe& draw : g_piece_draws)
			if (draw.room == s.room && draw.mesh == s.mesh)
				return true;

		return false;
	}

	static const char* Said(int answer)
	{
		return answer == HD_STATIC_ART ? "art" : answer == HD_STATIC_GROUPS ?
			"groups" : "classic";
	}

	// Whether a group the picture lists holds the static.
	static bool HolderListed(const HDStaticArt& a, const std::set<int>& listed)
	{
		for (int k = 0; k < a.groupCount; k++)
			if (a.holds[k] && listed.count(a.group[k]))
				return true;

		return false;
	}

	/* -statictable. The shares, binned so a gap shows: the old question (the
	 * cells of the box the room's own art holds, in a room drawn HD) and the
	 * new one per static (that, or where it does not reach a twentieth the
	 * largest share of the static's vertices another source holds), and
	 * every other source - the carrier's art drawn in every frame and each
	 * other group - on its own, for the statics the room's own art does not
	 * hold. */
	static void Table(const char* stem)
	{
		Answer(stem);

		static const double edges[] = { 0.0, 0.01, 0.02, 0.03, 0.04, 0.05,
			0.075, 0.10, 0.20, 0.50, 1e9 };
		const int bins = (int)(sizeof(edges) / sizeof(edges[0]));
		std::vector<int> oldBin((size_t)bins + 1, 0), newBin((size_t)bins + 1, 0);
		std::vector<int> otherBin((size_t)bins + 1, 0);
		int changed = 0, toArt = 0, toGroup = 0, toClassic = 0;
		int nArt = 0, nGroup = 0, nClassic = 0, oArt = 0, oClassic = 0;

		const auto bin = [&](double share)
		{
			if (share <= 0.0)
				return 0;

			for (int b = 1; b < bins; b++)
				if (share < edges[b])
					return b;

			return bins;
		};

		for (const Static& s : g_list)
		{
			const HDStaticArt& a = s.art;
			const bool hd = s.room < (int)g_stand_rooms_built.size() && StandGroupHD(s.room);
			int best = a.own > a.sure ? a.own : a.sure;

			const int verts = a.verts > 0 ? a.verts : 1;
			double share = (double)a.own / a.boxCells;

			(void)best;

			if (!HDStaticCellsHold(a.own, a.boxCells))
			{
				if (a.carrier >= 0)
				{
					otherBin[(size_t)bin((double)a.sure / verts)]++;

					if ((double)a.sure / verts > share)
						share = (double)a.sure / verts;
				}

				for (int k = 0; k < a.groupCount; k++)
				{
					otherBin[(size_t)bin((double)a.groupVerts[k] / verts)]++;

					if ((double)a.groupVerts[k] / verts > share)
						share = (double)a.groupVerts[k] / verts;
				}
			}

			oldBin[(size_t)bin(hd ? (double)a.own / a.boxCells : 0.0)]++;
			newBin[(size_t)bin(share)]++;

			const bool same = a.answer == s.oldAnswer;

			if (a.answer == HD_STATIC_ART) nArt++;
			else if (a.answer == HD_STATIC_GROUPS) nGroup++;
			else nClassic++;

			if (s.oldAnswer == HD_STATIC_ART) oArt++;
			else oClassic++;

			if (!same)
			{
				changed++;

				if (a.answer == HD_STATIC_ART) toArt++;
				else if (a.answer == HD_STATIC_GROUPS) toGroup++;
				else toClassic++;
			}

			char named[160];
			int at = 0;

			named[0] = 0;

			for (int k = 0; k < a.groupCount; k++)
				at += sprintf(named + at, "%s%d:%d%s", k ? "," : "", a.group[k],
					a.groupVerts[k], a.holds[k] ? "*" : "");

			// The piece, as the engine's HDLOG static line gives it.
			char piece[64];

			if (a.piece >= 0)
				sprintf(piece, "%d:%d held=%d open=%d others=%d", a.pieceGroup,
					a.pieceTris, a.pieceHeld, a.pieceOpen, a.pieceOthers);
			else if (a.answer == HD_STATIC_GROUPS)
				sprintf(piece, "- held=%d open=%d others=%d", a.pieceHeld, a.pieceOpen,
					a.pieceOthers);
			else
				sprintf(piece, "-");

			printf("statictable: %s room=%d mesh=%d slot=%d hd=%d flags=%d "
				"box=%.0f..%.0f,%.0f..%.0f,%.0f..%.0f cells=%d own=%d verts=%d "
				"carrier=%d sure=%d groups=%s old=%s new=%s%s piece=%s\n",
				stem, s.room, s.mesh, (int)s.slot, hd ? 1 : 0, (int)s.flags,
				s.mins[0], s.maxs[0], s.mins[1], s.maxs[1], s.mins[2], s.maxs[2],
				a.boxCells, a.own, a.verts, a.carrier, a.sure, at ? named : "-",
				Said(s.oldAnswer), Said(a.answer), same ? "" : " CHANGED", piece);
		}

		printf("statictable: %s statics=%d old art=%d classic=%d; new art=%d "
			"by_group=%d classic=%d; changed=%d (to art %d, to group %d, to "
			"classic %d)\n", stem, (int)g_list.size(), oArt, oClassic, nArt, nGroup,
			nClassic, changed, toArt, toGroup, toClassic);

		for (int b = 0; b <= bins; b++)
		{
			char name[48];

			if (!b)
				sprintf(name, "nothing");
			else if (b == bins)
				sprintf(name, "all");
			else if (edges[b] > 1.0)
				sprintf(name, ">= %.3f", edges[b - 1]);
			else
				sprintf(name, "%.3f..%.3f", edges[b - 1], edges[b]);

			if (oldBin[(size_t)b] || newBin[(size_t)b] || otherBin[(size_t)b])
				printf("statictable: %s share %-13s old %5d new %5d other %5d\n",
					stem, name, oldBin[(size_t)b], newBin[(size_t)b],
					otherBin[(size_t)b]);
		}

		/* The shatterable ones and their pieces, as BuildStaticTables pairs
		 * them: every piece of a group drawn HD whose box overlaps the
		 * static's, in the piece group's frame. */
		for (const Static& s : g_list)
		{
			if (!HDStaticShatterSlot(s.slot))
				continue;

			int found = 0;

			for (int p = 0; p < HDRenderDestructibleCount(); p++)
			{
				int group = -1;
				float lo[3], hi[3];

				if (!HDRenderDestructibleInfo(p, &group, 0, lo, hi) || group < 0 ||
					group >= (int)g_stand_rooms_built.size() ||
					!StandGroupHD(group) ||
					!HDStaticPieceInState(Twin(s.room), group))
					continue;

				const float dx = g_stand_rooms_built[(size_t)s.room].origin[0] -
					g_stand_rooms_built[(size_t)group].origin[0];
				const float dz = g_stand_rooms_built[(size_t)s.room].origin[2] -
					g_stand_rooms_built[(size_t)group].origin[2];

				if (!HDStaticPiecePaired(s.mins, s.maxs, dx, dz, lo, hi))
					continue;

				found++;
				printf("statictable: %s shatter room=%d mesh=%d slot=%d answer=%s "
					"piece=%d group=%d%s\n", stem, s.room, s.mesh, (int)s.slot,
					Said(s.art.answer), p, group, group == s.room ? "" : " other");
			}

			if (!found)
				printf("statictable: %s shatter room=%d mesh=%d slot=%d answer=%s "
					"piece=none%s\n", stem, s.room, s.mesh, (int)s.slot,
					Said(s.art.answer), s.art.answer == HD_STATIC_CLASSIC ? "" :
					" DEFECT - left to the art with no piece");
		}
	}

	/* The origin shift of a static's room into a piece's group frame, as
	 * hd_level.cpp hands it to HDStaticPiecePaired. */
	static void SmashShift(const Static& s, int group, float* dx, float* dz)
	{
		*dx = g_stand_rooms_built[(size_t)s.room].origin[0] -
			g_stand_rooms_built[(size_t)group].origin[0];
		*dz = g_stand_rooms_built[(size_t)s.room].origin[2] -
			g_stand_rooms_built[(size_t)group].origin[2];
	}

	/* -smashcensus: every destructible piece of the art, every shatterable
	 * static (slots 50 to 59, HDStaticShatterSlot) and, for each static,
	 * every piece of a group drawn HD within 1024 units on all three axes:
	 * the gap per axis after the origin shift (negative is the overlap's
	 * length) and whether BuildStaticTables pairs the two - a piece of the
	 * room's flipmap twin never, its line ending in ` twin`. The summary
	 * counts the pieces no static takes and the pieces two or more statics
	 * take - hd_level.cpp's UpdateDestructibles writes a piece's state once
	 * per pair, so the last pair of a shared piece decides it. */
	static void SmashCensus(const char* stem)
	{
		Answer(stem);

		const int pieces = HDRenderDestructibleCount();
		std::vector<int> takers((size_t)(pieces > 0 ? pieces : 1), 0);
		int statics = 0, pairs = 0, without = 0;
		const int groups = (int)g_stand_rooms_built.size();

		for (int p = 0; p < pieces; p++)
		{
			int group = -1;
			unsigned int material = 0;
			float lo[3], hi[3];

			if (!HDRenderDestructibleInfo(p, &group, &material, lo, hi))
				continue;

			printf("smash: %s piece=%d group=%d material=0x%04x base=%u "
				"copy=%u box=%.0f..%.0f,%.0f..%.0f,%.0f..%.0f hd=%d\n", stem,
				p, group, material, material & 0xFFu, (material >> 8) & 0x7Fu,
				lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], group >= 0 &&
				group < groups && StandGroupHD(group) ? 1 : 0);
		}

		for (const Static& s : g_list)
		{
			if (!HDStaticShatterSlot(s.slot))
				continue;

			statics++;

			const char* flat = "-";

			for (int axis = 0; axis < 3 && flat[0] == '-'; axis++)
				if (s.maxs[axis] - s.mins[axis] < 1.0f)
					flat = axis == 0 ? "x" : axis == 1 ? "y" : "z";

			printf("smash: %s static room=%d mesh=%d slot=%d flags=%d "
				"box=%.0f..%.0f,%.0f..%.0f,%.0f..%.0f flat=%s\n", stem, s.room,
				s.mesh, (int)s.slot, (int)s.flags, s.mins[0], s.maxs[0],
				s.mins[1], s.maxs[1], s.mins[2], s.maxs[2], flat);

			int taken = 0;

			for (int p = 0; p < pieces; p++)
			{
				int group = -1;
				float lo[3], hi[3];

				if (!HDRenderDestructibleInfo(p, &group, 0, lo, hi) ||
					group < 0 || group >= groups || !StandGroupHD(group))
					continue;

				float dx, dz;

				SmashShift(s, group, &dx, &dz);

				const float slo[3] =
					{ s.mins[0] + dx, s.mins[1], s.mins[2] + dz };
				const float shi[3] =
					{ s.maxs[0] + dx, s.maxs[1], s.maxs[2] + dz };
				float gap[3];
				bool reach = true;

				for (int axis = 0; axis < 3; axis++)
				{
					const float from =
						slo[axis] > lo[axis] ? slo[axis] : lo[axis];
					const float to =
						shi[axis] < hi[axis] ? shi[axis] : hi[axis];

					gap[axis] = from - to;
					reach = reach && gap[axis] <= 1024.0f;
				}

				if (!reach)
					continue;

				const bool state = HDStaticPieceInState(Twin(s.room), group);
				const bool paired = state && HDStaticPiecePaired(s.mins, s.maxs,
					dx, dz, lo, hi);

				printf("smash: %s near room=%d mesh=%d piece=%d group=%d "
					"gap=%.0f,%.0f,%.0f paired=%d%s\n", stem, s.room, s.mesh, p,
					group, gap[0], gap[1], gap[2], paired ? 1 : 0,
					state ? "" : " twin");

				if (paired)
				{
					takers[(size_t)p]++;
					pairs++;
					taken++;
				}
			}

			if (!taken)
				without++;
		}

		int unpaired = 0, shared = 0;

		for (int p = 0; p < pieces; p++)
		{
			int group = -1;

			if (!HDRenderDestructibleInfo(p, &group, 0, 0, 0) || group < 0 ||
				group >= groups || !StandGroupHD(group))
				continue;

			if (!takers[(size_t)p])
				unpaired++;
			else if (takers[(size_t)p] > 1)
				shared++;
		}

		printf("smash: %s pieces=%d statics=%d pairs=%d pieces_unpaired=%d "
			"pieces_shared=%d statics_without=%d\n", stem, pieces, statics,
			pairs, unpaired, shared, without);
	}

	/* -smash <room>:<mesh>: the art after Lara has shot those statics, as
	 * the engine draws it. The pairs are hd_level.cpp's - HDStaticPiecePaired
	 * over every piece of a group drawn HD but the room's flipmap twin's
	 * (HDStaticPieceInState), statics in room and mesh order - and each pair
	 * writes its piece's state the way UpdateDestructibles does, hidden when
	 * its static is shot and shown when not, so a piece two statics take
	 * ends in the state of the last of them. With -flip the shots name the
	 * rooms as they stand after those flips: a pair reads the static where
	 * its list stands now (HDStaticListRoom over the group table FlipMap
	 * and HDFlipRoom leave), and its line ends in ` at=` that room. */
	static void SmashHide(const char* stem,
		const std::vector<std::pair<int, int> >& shot)
	{
		Answer(stem);

		const int groups = (int)g_stand_rooms_built.size();
		const int count = g_level.roomCount();
		std::vector<short> table((size_t)(count > 0 ? count : 1), -1);

		for (int r = 0; r < count; r++)
			table[(size_t)r] = (short)r;

		for (size_t k = 0; k < walkoff::g_flip_rooms.size(); k++)
		{
			const int a = walkoff::g_flip_rooms[k];
			const int b = a >= 0 && a < count ?
				g_level.rooms()[(size_t)a].altRoom : -1;

			if (b >= 0 && b < count)
				std::swap(table[(size_t)a], table[(size_t)b]);
		}

		for (const Static& s : g_list)
		{
			const int at = HDStaticListRoom(&table[0], count, s.room);
			bool named = false;

			for (size_t k = 0; k < shot.size(); k++)
				named = named || (shot[k].first == at &&
					shot[k].second == s.mesh);

			if (!HDStaticShatterSlot(s.slot))
				continue;

			for (int p = 0; p < HDRenderDestructibleCount(); p++)
			{
				int group = -1;
				float lo[3], hi[3];

				if (!HDRenderDestructibleInfo(p, &group, 0, lo, hi) ||
					group < 0 || group >= groups || !StandGroupHD(group) ||
					!HDStaticPieceInState(Twin(s.room), group))
					continue;

				float dx, dz;

				SmashShift(s, group, &dx, &dz);

				if (!HDStaticPiecePaired(s.mins, s.maxs, dx, dz, lo, hi))
					continue;

				char moved[24] = "";

				if (at != s.room)
					snprintf(moved, sizeof(moved), " at=%d", at);

				HDRenderDestructibleHide(p, named);
				printf("smash: %s %s room=%d mesh=%d piece=%d group=%d%s\n",
					stem, named ? "hidden" : "shown", s.room, s.mesh, p, group,
					moved);
			}
		}
	}

	struct Vtx
	{
		float x, y, z;
		DWORD colour;
	};

	/* One static's mesh, as triangles in the seed's frame: the mesh's own
	 * vertices turned by its y_rot through the engine's sine table and moved
	 * to its place. A mesh is a 12-byte head (the centre, the radius, the
	 * vertex count in the low byte of the word at +10), the vertices, the
	 * normals or the lights, then the quads (four corners, the texture and
	 * the effects) and the triangles (three corners and the same two). */
	static void Mesh(const Static& s, double ox, double oy, double oz,
		std::vector<Vtx>& out)
	{
		size_t at = 0;
		const std::vector<uint8_t>& raw = g_level.debugBytes();

		if (!g_have[s.slot] || !g_level.debugMeshAt(g_mesh_of[s.slot], at) ||
			at + 12 > raw.size())
			return;

		const auto s16 = [&](size_t p) -> int
		{
			short v = 0;

			if (p + 2 <= raw.size())
				memcpy(&v, raw.data() + p, 2);

			return v;
		};

		const int verts = s16(at + 10) & 0xFF;
		size_t p = at + 12;
		std::vector<double> pos((size_t)verts * 3);
		const double c = (double)enginecam::PhdCos(s.rot) / 16384.0;
		const double sn = (double)enginecam::PhdSin(s.rot) / 16384.0;

		for (int v = 0; v < verts; v++, p += 6)
		{
			const double x = s16(p), y = s16(p + 2), z = s16(p + 4);

			pos[(size_t)v * 3 + 0] = (double)s.x + x * c + z * sn - ox;
			pos[(size_t)v * 3 + 1] = (double)s.y + y - oy;
			pos[(size_t)v * 3 + 2] = (double)s.z + z * c - x * sn - oz;
		}

		const int norms = s16(p);

		p += 2 + (size_t)verts * (norms >= 0 ? 6 : 2);

		const auto tri = [&](int a, int b, int d)
		{
			if (a < 0 || b < 0 || d < 0 || a >= verts || b >= verts || d >= verts)
				return;

			const double* q[3] = { &pos[(size_t)a * 3], &pos[(size_t)b * 3], &pos[(size_t)d * 3] };
			const double e1[3] = { q[1][0] - q[0][0], q[1][1] - q[0][1], q[1][2] - q[0][2] };
			const double e2[3] = { q[2][0] - q[0][0], q[2][1] - q[0][1], q[2][2] - q[0][2] };
			const double n[3] =
			{
				e1[1] * e2[2] - e1[2] * e2[1],
				e1[2] * e2[0] - e1[0] * e2[2],
				e1[0] * e2[1] - e1[1] * e2[0]
			};
			const double length = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

			if (length <= 0.0)
				return;

			// Greener than the faces, so a static reads as a static.
			const double l[3] = { 0.35, -0.8, 0.48 };
			const double lit = fabs((n[0] * l[0] + n[1] * l[1] + n[2] * l[2]) / length);
			const int grey = 60 + (int)(140.0 * lit);
			const DWORD colour = D3DCOLOR_XRGB(grey / 2, grey, grey / 2);

			for (int k = 0; k < 3; k++)
			{
				Vtx v;

				v.x = (float)q[k][0];
				v.y = (float)q[k][1];
				v.z = (float)q[k][2];
				v.colour = colour;
				out.push_back(v);
			}
		};

		const int quads = s16(p);

		p += 2;

		for (int i = 0; i < quads && quads > 0; i++, p += 12)
		{
			const int a = s16(p), b = s16(p + 2), d = s16(p + 4), e = s16(p + 6);

			tri(a, b, d);
			tri(a, d, e);
		}

		const int tris = quads >= 0 ? s16(p) : 0;

		p += 2;

		for (int i = 0; i < tris && tris > 0; i++, p += 10)
			tri(s16(p), s16(p + 2), s16(p + 4));
	}

	/* The groups this picture lists - the seed and the -with groups, less
	 * -skipgroup - and the rooms: those and the -classicroom rooms. */
	static void Listed(int seed, std::set<int>& artListed, std::set<int>& rooms)
	{
		if (!g_skip_groups.count(seed))
			artListed.insert(seed);

		for (int i = 0; i < g_with_count; i++)
			if (!g_skip_groups.count(g_with_group[i]))
				artListed.insert(g_with_group[i]);

		rooms = artListed;

		for (int r : g_classic_rooms)
			rooms.insert(r);

		for (int r : g_list_rooms)
			rooms.insert(r);
	}

	// Whether -piece leaves a static to this picture: all, or those named.
	static bool Named(const Static& s)
	{
		bool named = g_piece_only.empty();

		for (const std::pair<int, int>& only : g_piece_only)
			named = named || (only.first == s.room && only.second == s.mesh);

		return named;
	}

	/* -statics new: the pieces of this picture, before the bracket, so the
	 * frame draws them with the groups and Gather leaves their statics out. */
	static void PreparePieces(const char* stem, int seed)
	{
		g_piece_draws.clear();

		if (g_rule != 1 || !Load(stem))
			return;

		Answer(stem);

		if (seed < 0 || seed >= (int)g_stand_rooms_built.size())
			return;

		std::set<int> artListed, rooms;

		Listed(seed, artListed, rooms);

		for (const Static& s : g_list)
		{
			const HDStaticArt& a = s.art;

			if (!rooms.count(s.room) || !(s.flags & 1) || a.answer != HD_STATIC_GROUPS ||
				HolderListed(a, artListed) || !HDRenderStaticPieceReady(a.piece) ||
				a.pieceGroup < 0 || a.pieceGroup >= (int)g_stand_rooms_built.size())
				continue;

			if (!Named(s))
				continue;

			PieceDrawProbe draw;

			memset(&draw.world, 0, sizeof(draw.world));
			draw.world._11 = draw.world._22 = draw.world._33 = draw.world._44 = 1.0f;
			draw.world._41 = g_stand_rooms_built[(size_t)a.pieceGroup].origin[0] -
				g_stand_rooms_built[(size_t)seed].origin[0];
			draw.world._42 = g_stand_rooms_built[(size_t)a.pieceGroup].origin[1] -
				g_stand_rooms_built[(size_t)seed].origin[1];
			draw.world._43 = g_stand_rooms_built[(size_t)a.pieceGroup].origin[2] -
				g_stand_rooms_built[(size_t)seed].origin[2];
			draw.piece = a.piece;
			draw.room = s.room;
			draw.mesh = s.mesh;
			g_piece_draws.push_back(draw);
			printf("statics: room %d mesh %d slot %d drawn as its piece, group %d "
				"(%d triangle(s))\n", s.room, s.mesh, (int)s.slot, a.pieceGroup,
				a.pieceTris);
		}
	}

	static void DrawPieces(const HDRoomLighting* lighting, unsigned int passes)
	{
		for (const PieceDrawProbe& draw : g_piece_draws)
			HDRenderStaticPiece(draw.piece, &draw.world, lighting, passes);
	}

	/* The statics this picture draws, as triangles in the seed's frame, and
	 * how many. */
	static int Gather(const char* stem, int seed, std::vector<Vtx>& out)
	{
		if ((!g_rule && g_forced.empty()) || !Load(stem))
			return 0;

		if (g_rule == 1)
			Answer(stem);

		if (seed < 0 || seed >= (int)g_stand_rooms_built.size())
			return 0;

		const double ox = g_stand_rooms_built[(size_t)seed].origin[0];
		const double oy = g_stand_rooms_built[(size_t)seed].origin[1];
		const double oz = g_stand_rooms_built[(size_t)seed].origin[2];

		std::set<int> artListed;	// the groups this picture lists
		std::set<int> rooms;		// the rooms it lists

		Listed(seed, artListed, rooms);

		int drawn = 0;

		for (const Static& s : g_list)
		{
			bool want = false;

			for (const std::pair<int, int>& f : g_forced)
				want = want || (f.first == s.room && f.second == s.mesh);

			if (!want && g_rule && rooms.count(s.room) && (s.flags & 1) && Named(s))
			{
				if (g_rule == 1)
					want = HDStaticDrawSource(s.art.answer,
						HolderListed(s.art, artListed), PieceDrawn(s)) ==
						HD_STATIC_FROM_CLASSIC;
				else
				{
					/* The rule before: a room drawn HD draws a static only
					 * where its own group has under a twentieth; a room drawn
					 * classic draws all. */
					if (!StandGroupHD(s.room))
						want = true;
					else
					{
						HDStaticArt own;
						std::vector<HDStaticGroup> one(g_stand_rooms_built.size());

						for (size_t g = 0; g < one.size(); g++)
						{
							one[g].hd = 0;
							one[g].state = HD_STAND_ROOM_CLASSIC;
							one[g].carrier = -1;
							one[g].originX = g_stand_rooms_built[g].origin[0];
							one[g].originZ = g_stand_rooms_built[g].origin[2];
						}

						one[(size_t)s.room].hd = 1;
						HDRenderStaticArt(s.room, s.mins, s.maxs, 0, 0, &one[0],
							(int)one.size(), -1, &own);
						want = !HDStaticCellsHold(own.own, own.boxCells);
					}
				}
			}

			if (!want)
				continue;

			const size_t before = out.size();

			Mesh(s, ox, oy, oz, out);
			drawn++;
			printf("statics: room %d mesh %d slot %d drawn classic (%d triangle(s))\n",
				s.room, s.mesh, (int)s.slot, (int)((out.size() - before) / 3));
		}

		return drawn;
	}
}

/* The classic faces of -classicroom, drawn into the finished picture - see the
 * note at g_classic_rooms. Its own scene and its own state block, after the
 * state bracket of the HD frame has been checked, so the check is the HD
 * path's alone. */
/* -classicclip: each room's classic faces drawn inside that room's window
 * of -roomwin, as the classic path draws a room inside its portal window,
 * and not at all for a room with no window there or an empty one. Off, the
 * faces are drawn whole over the screen. */
static bool g_classic_clip = false;

static void DrawClassicFaces(const char* stem, int seed, const float* view,
	const D3DMATRIX& projection, double ex, double ey, double ez, long width,
	long height)
{
	if (g_classic_rooms.empty() && !probestatics::g_rule &&
		probestatics::g_forced.empty())
		return;

	if (g_roombox.empty())
	{
		printf("classic: -classicroom and the statics need -roombox <folder "
			"holding the classic .TRC>\n");
		return;
	}

	Tr5Level level;
	std::string error;
	const std::string trc = g_roombox + "\\" + stem + ".TRC";
	std::wstring wide;

	for (size_t i = 0; i < trc.size(); i++)
		wide.push_back((wchar_t)(unsigned char)trc[i]);

	if (!level.load(wide, error) || seed < 0 || seed >= level.roomCount())
	{
		printf("classic: %s could not be read (%s)\n", trc.c_str(), error.c_str());
		return;
	}

	// The seed group's own frame: x and z from its origin, y world.
	unsigned yOrigin = 0;

	level.headerField(seed, 24, yOrigin, error);

	const double ox = (double)level.rooms()[(size_t)seed].originX;
	const double oy = (double)(int)yOrigin;
	const double oz = (double)level.rooms()[(size_t)seed].originZ;

	typedef probestatics::Vtx Vtx;

	// [0] single-sided, culled like the art's opaque ranges; [1] double-sided.
	std::vector<Vtx> faces[2];
	// Where each room's faces begin in the two, for -classicclip.
	std::vector<std::pair<int, size_t> > starts[2];
	int count = 0;
	// The static meshes of -statics and -static, both sides drawn.
	std::vector<Vtx> statics[2];
	const int staticCount = probestatics::Gather(stem, seed, statics[1]);

	for (int room : g_classic_rooms)
	{
		std::vector<Tr5Level::FaceInfo> list;
		const unsigned char* dropped = 0;
		const int state = g_stand_rule ? StandState(room, 0, &dropped) :
			HD_STAND_ROOM_CLASSIC;

		if (state == HD_STAND_ROOM_COVERED)
		{
			printf("classic: room %d is covered by stand art - no classic face "
				"drawn (-stand)\n", room);
			continue;
		}

		if (room < 0 || room >= level.roomCount() || !level.roomFaces(room, list, error))
		{
			printf("classic: room %d has no faces to read (%s)\n", room, error.c_str());
			continue;
		}

		int left = 0;
		std::vector<Tr5Level::SectorInfo> squares;

		starts[0].push_back(std::make_pair(room, faces[0].size()));
		starts[1].push_back(std::make_pair(room, faces[1].size()));

		if (g_classic_wet)
			level.sectorInfo(room, squares, error);

		for (size_t f = 0; f < list.size(); f++)
		{
			const Tr5Level::FaceInfo& face = list[f];
			const int corners = face.quad ? 4 : 3;

			/* -classicplane: only the faces in that plane. -classicwet: of
			 * those not the dry copies hd_level.cpp's BuildWaterSurfaces
			 * leaves out beside a sheet - over a square with no water room
			 * below whose plane the art draws flat there. */
			if (g_classic_plane_set)
			{
				bool in = true;
				long x0 = 1 << 30, x1 = -(1 << 30);
				long z0 = 1 << 30, z1 = -(1 << 30);

				for (int c = 0; c < corners; c++)
				{
					in = in && face.v[c][1] == g_classic_plane;
					x0 = face.v[c][0] < x0 ? face.v[c][0] : x0;
					x1 = face.v[c][0] > x1 ? face.v[c][0] : x1;
					z0 = face.v[c][2] < z0 ? face.v[c][2] : z0;
					z1 = face.v[c][2] > z1 ? face.v[c][2] : z1;
				}

				if (!in)
					continue;

				if (g_classic_wet)
				{
					const Tr5Room& rm = level.rooms()[(size_t)room];
					const long lx0 = x0 - rm.originX, lx1 = x1 - rm.originX;
					const long lz0 = z0 - rm.originZ, lz1 = z1 - rm.originZ;
					const int sx = (int)((lx0 + lx1) / 2 >> 10);
					const int sz = (int)((lz0 + lz1) / 2 >> 10);
					bool water = false;

					for (const Tr5Level::SectorInfo& q : squares)
						if (q.sx == sx && q.sz == sz && q.roomBelow >= 0 &&
							q.roomBelow < level.roomCount())
							water = (level.rooms()[(size_t)q.roomBelow].flags &
								1) != 0;

					if (!water && HDRenderGroupFlatArt(room,
						(int)g_classic_plane, (int)lx0, (int)lz0, (int)lx1,
						(int)lz1))
					{
						left++;
						continue;
					}
				}
			}

			// A face the carrier covers is drawn as its art (-stand).
			if (dropped && dropped[f])
			{
				left++;
				continue;
			}

			for (int half = 0; half + 2 < corners; half++)
			{
				const int at[3] = { 0, half + 1, half + 2 };
				double p[3][3];

				for (int c = 0; c < 3; c++)
				{
					p[c][0] = (double)face.v[at[c]][0] - ox;
					p[c][1] = (double)face.v[at[c]][1] - oy;
					p[c][2] = (double)face.v[at[c]][2] - oz;
				}

				const double e1[3] = { p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2] };
				const double e2[3] = { p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2] };
				double n[3] =
				{
					e1[1] * e2[2] - e1[2] * e2[1],
					e1[2] * e2[0] - e1[0] * e2[2],
					e1[0] * e2[1] - e1[1] * e2[0]
				};
				const double length = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

				if (length <= 0.0)
					continue;

				// A light from above and to one side, y down: walls and floors
				// come out in different greys, so a face reads as a face.
				const double l[3] = { 0.35, -0.8, 0.48 };
				const double lit = fabs((n[0] * l[0] + n[1] * l[1] + n[2] * l[2]) / length);
				const int grey = 70 + (int)(150.0 * lit);
				const DWORD colour = g_classic_mark.count(room)
					? D3DCOLOR_XRGB(grey, 0, 0)
					: D3DCOLOR_XRGB(grey, grey, grey - 20 > 0 ? grey - 20 : 0);

				for (int c = 0; c < 3; c++)
				{
					Vtx v;

					v.x = (float)p[c][0];
					v.y = (float)p[c][1];
					v.z = (float)p[c][2];
					v.colour = colour;
					faces[face.doubleSided ? 1 : 0].push_back(v);
				}
			}

			count++;
		}

		if (dropped)
			printf("classic: room %d drawn in part - %d face(s) left to the "
				"carrier's art, %d kept (-stand)\n", room, left, (int)list.size() - left);
	}

	printf("classic: %d face(s) of %d room(s) drawn flat, %d triangle(s) "
		"single-sided and %d double-sided\n", count, (int)g_classic_rooms.size(),
		(int)faces[0].size() / 3, (int)faces[1].size() / 3);

	if (probestatics::g_rule || !probestatics::g_forced.empty())
		printf("statics: %d static(s) drawn flat, %d triangle(s), rule %s\n",
			staticCount, (int)statics[1].size() / 3, probestatics::g_rule == 1 ? "new" :
			probestatics::g_rule == 2 ? "old" : "none (-static only)");

	IDirect3DStateBlock9* saved = 0;

	g_device->CreateStateBlock(D3DSBT_ALL, &saved);
	g_device->BeginScene();

	D3DMATRIX identity;

	memset(&identity, 0, sizeof(identity));
	identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
	g_device->SetTransform(D3DTS_WORLD, &identity);
	g_device->SetTransform(D3DTS_VIEW, (const D3DMATRIX*)view);
	g_device->SetTransform(D3DTS_PROJECTION, &projection);
	g_device->SetVertexShader(0);
	g_device->SetPixelShader(0);
	g_device->SetTexture(0, 0);
	g_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	g_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	g_device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	g_device->SetRenderState(D3DRS_LIGHTING, FALSE);
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	g_device->SetRenderState(D3DRS_CLIPPING, TRUE);

	/* The faces, `shift` units along the line of sight towards the eye
	 * (negative: away from it). */
	const std::vector<Vtx>* set = faces;

	const auto draw = [&](double shift)
	{
		for (int sided = 0; sided < 2; sided++)
		{
			if (set[sided].empty())
				continue;

			std::vector<Vtx> moved = set[sided];

			if (shift != 0.0)
				for (Vtx& v : moved)
				{
					const double d[3] = { ex - v.x, ey - v.y, ez - v.z };
					const double length = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);

					if (length <= 0.0)
						continue;

					v.x += (float)(d[0] / length * shift);
					v.y += (float)(d[1] / length * shift);
					v.z += (float)(d[2] / length * shift);
				}

			g_device->SetRenderState(D3DRS_CULLMODE, sided ? D3DCULL_NONE : D3DCULL_CCW);
			g_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);

			if (!g_classic_clip || set != faces)
			{
				g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
					(UINT)(moved.size() / 3), &moved[0], sizeof(Vtx));
				continue;
			}

			for (size_t k = 0; k < starts[sided].size(); k++)
			{
				const size_t from = starts[sided][k].second;
				const size_t to = k + 1 < starts[sided].size()
					? starts[sided][k + 1].second : moved.size();
				const long* win = 0;

				for (int w = 0; w < g_roomwin_count && !win; w++)
					if (g_roomwin_group[w] == starts[sided][k].first)
						win = g_roomwin_box[w];

				if (to <= from || !win || win[0] > win[2] || win[1] > win[3])
					continue;

				const RECT r = { win[0], win[1], win[2] + 1, win[3] + 1 };

				g_device->SetScissorRect(&r);
				g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
				g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
					(UINT)((to - from) / 3), &moved[from], sizeof(Vtx));
				g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
			}
		}
	};

	struct Flat
	{
		float x, y, z, rhw;
		DWORD colour;
	};

	const Flat screen[4] =
	{
		{ -0.5f, -0.5f, 0.0f, 1.0f, D3DCOLOR_XRGB(255, 255, 0) },
		{ (float)width - 0.5f, -0.5f, 0.0f, 1.0f, D3DCOLOR_XRGB(255, 255, 0) },
		{ -0.5f, (float)height - 0.5f, 0.0f, 1.0f, D3DCOLOR_XRGB(255, 255, 0) },
		{ (float)width - 0.5f, (float)height - 0.5f, 0.0f, 1.0f, D3DCOLOR_XRGB(255, 255, 0) }
	};

	if (g_classic_only || probestatics::g_only)
		g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
			D3DCOLOR_XRGB(255, 0, 255), 1.0f, 0);

	/* The faces first, then the statics, each counted on its own against
	 * the art alone: the second set is counted before the first is drawn. */
	for (int which = 0; which < 2 && g_coincide > 0.0; which++)
	{
		set = which ? statics : faces;

		if (set[0].empty() && set[1].empty())
			continue;

		g_device->Clear(0, 0, D3DCLEAR_STENCIL, 0, 1.0f, 0);
		g_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
		g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		g_device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
		g_device->SetRenderState(D3DRS_STENCILMASK, 0xFF);
		g_device->SetRenderState(D3DRS_STENCILWRITEMASK, 0xFF);
		g_device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
		g_device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);

		// In front of the art or within `units` behind it: 1.
		g_device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
		g_device->SetRenderState(D3DRS_STENCILREF, 1);
		g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
		draw(g_coincide);

		// Still in front of it from `units` further away: not within, 2.
		g_device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
		g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_INCR);
		draw(-g_coincide);

		// The pixels at stencil `ref`, counted; -1 without an occlusion query.
		const auto count = [&](DWORD ref) -> long
		{
			IDirect3DQuery9* query = 0;
			DWORD pixels = 0;

			g_device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
			g_device->SetRenderState(D3DRS_STENCILREF, ref);
			g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
			g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
			g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			g_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

			if (FAILED(g_device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query)))
				return -1;

			query->Issue(D3DISSUE_BEGIN);
			g_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, screen, sizeof(Flat));
			query->Issue(D3DISSUE_END);

			while (query->GetData(&pixels, sizeof(pixels), D3DGETDATA_FLUSH) == S_FALSE)
				;

			query->Release();
			g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
			return (long)pixels;
		};

		// The ones left at 1, counted.
		const long shared = count(1);

		/* Of those, the ones the classic face wins: the faces once more where
		 * they are, depth test as the frame's, and a pixel at 1 that passes
		 * gets bit 4 (5). The ones still at 1 are the art's - where the art
		 * stands in front of the face within `units`, in the depth the
		 * renderer wrote (an art range drawn pushed back loses those). */
		long artWins = -1;

		if (shared >= 0)
		{
			g_device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
			g_device->SetRenderState(D3DRS_STENCILREF, 5);
			g_device->SetRenderState(D3DRS_STENCILMASK, 0x3);
			g_device->SetRenderState(D3DRS_STENCILWRITEMASK, 0x4);
			g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
			draw(0.0);
			g_device->SetRenderState(D3DRS_STENCILMASK, 0xFF);
			g_device->SetRenderState(D3DRS_STENCILWRITEMASK, 0xFF);
			artWins = count(1);
		}

		/* In front of the art by more than `units`: still at 2 after the
		 * second pass. For a static, that is where the art has nothing for
		 * it. */
		const long beyond = shared >= 0 ? count(2) : -1;

		if (shared < 0)
			printf("coincide: this device has no occlusion query - no count\n");
		else if (!which)
			printf("coincide: %ld pixel(s) of %ld at which a classic face lands "
				"within %.0f units of the art, %ld of them the art's\n", shared,
				width * height, g_coincide, artWins);
		else
			printf("coincide statics: within=%ld of %ld at which a classic static "
				"lands within %.0f units of the art, %ld of them the art's; "
				"beyond=%ld in front of it by more\n", shared, width * height,
				g_coincide, artWins, beyond);

		g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
		g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
		g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		g_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
	}

	g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	set = faces;
	draw(0.0);
	set = statics;
	draw(0.0);
	set = faces;

	if (g_coincide > 0.0 && g_coincide_mark)
	{
		// Yellow where the two share a place (1 and 5), red where the art
		// wins it (1).
		Flat red[4];

		for (int c = 0; c < 4; c++)
		{
			red[c] = screen[c];
			red[c].colour = D3DCOLOR_XRGB(255, 0, 0);
		}

		g_device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
		g_device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
		g_device->SetRenderState(D3DRS_STENCILREF, 1);
		g_device->SetRenderState(D3DRS_STENCILMASK, 0x3);
		g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
		g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
		g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		g_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
		g_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, screen, sizeof(Flat));
		g_device->SetRenderState(D3DRS_STENCILMASK, 0xFF);
		g_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, red, sizeof(Flat));
	}

	g_device->EndScene();

	if (saved)
	{
		saved->Apply();
		saved->Release();
	}
}

/* -watermarks: the classic water marks the engine hands the renderer at level
 * load (hd_level.cpp, BuildWaterMarks), read out of the -roombox .TRC. A twin
 * of that function and of the predicate it asks per vertex, drawroom.cpp's
 * aRoomVertexWaterMarks with IsMistVert, IsReflectionVert and IsShoreVert:
 * those walk the engine's ROOM_INFO array and globals and cannot be linked
 * into a tool. Same order, same tests, the same float arithmetic, so a
 * corner matches exactly where the engine's does; the engine's log line
 * 'water marks=<n> rooms=<n>' is printed the same way to hold the two
 * against each other.
 *
 * The room blob of a .TRC is the engine's ROOM_INFO, 208 bytes, then its
 * body, and every pointer in the header is an offset into the body
 * (alexstuff.cpp, aFixUpRoom): the door list at +4, x at +20, z at +28,
 * minfloor at +32, flipped_room at +76, the flags at +78, the roomlets'
 * count at +168, the roomlets at +172 and their vertices at +176
 * (global/types.h, ROOM_INFO).
 * A roomlet is 56 bytes with nVtx at +0 and nWVtx at +2 (ROOMLET), and its
 * vertices follow the previous roomlet's: seven floats each, x and z room
 * local, y world. A door list is a short count and 32 bytes a portal - the
 * room, the normal, four corners (ROOM_PORTAL). An odd door offset is no
 * door list, the test aFixUpRoom makes. */
namespace probemarks
{
	struct Portal
	{
		short rn;
		short normal[3];
		short v1[3], v2[3], v3[3], v4[3];
	};

	struct Roomlet
	{
		long nVtx;
		long nWVtx;
		size_t first;		// into Room::vtx, in floats
	};

	struct Room
	{
		long x = 0, z = 0, minfloor = 0;
		unsigned short flags = 0;
		short flipped = -1;		// flipped_room at +76, -1 for none
		bool door = false;
		std::vector<Portal> portals;
		std::vector<Roomlet> roomlets;
		std::vector<float> vtx;
	};

	// global/types.h, room_flags.
	const unsigned short kUnderwater = 0x1;
	const unsigned short kMist = 0x100;
	const unsigned short kReflect = 0x200;
	const unsigned short kReflectCeiling = 0x1000;
	// hd_level.cpp, HD_MAX_ROOMS: the rooms BuildWaterMarks walks at most.
	const int kMaxRooms = 1024;

	std::vector<Room> g_rooms;
	std::vector<HDWaterMark> g_marks;
	// The portals the closed doors shut before the marks (ShutClosedDoors).
	int g_shut = 0;

	// One of a room's vertices at the world position x, y, z.
	bool HasVertex(const Room& r, float x, float y, float z)
	{
		for (const Roomlet& l : r.roomlets)
			for (long n = 0; n < l.nVtx; n++)
			{
				const float* v = &r.vtx[l.first + (size_t)n * 7];

				if (x == v[0] + r.x && y == v[1] && z == v[2] + r.z)
					return true;
			}

		return false;
	}

	bool Valid(int rn)
	{
		return rn >= 0 && rn < (int)g_rooms.size();
	}

	// The portal's box test the three predicates share.
	bool Outside(const Portal& p, const float* v)
	{
		return v[0] < p.v1[0] || v[0] > p.v3[0] || v[2] < p.v1[2] ||
			v[2] > p.v2[2];
	}

	// IsMistVert: no vertex at the same place behind a downward portal.
	bool Mist(const Room& cur, const float* v)
	{
		if (!cur.door)
			return false;

		const float x = v[0] + cur.x, y = v[1], z = v[2] + cur.z;

		for (const Portal& p : cur.portals)
		{
			if (p.normal[1] != -1 || Outside(p, v) || !Valid(p.rn))
				continue;

			if (HasVertex(g_rooms[(size_t)p.rn], x, y, z))
				return false;
		}

		return true;
	}

	/* IsReflectionVert, with its side answer has_water_neighbor: an
	 * underwater room behind a downward portal over the vertex. */
	bool Reflection(const Room& cur, const float* v, bool& waterNeighbour)
	{
		waterNeighbour = false;

		if (!cur.door)
			return false;

		const float x = v[0] + cur.x, y = v[1], z = v[2] + cur.z;
		bool cont = false;

		for (const Portal& p : cur.portals)
		{
			if (p.normal[1] != -1 || Outside(p, v) || !Valid(p.rn))
				continue;

			const Room& r = g_rooms[(size_t)p.rn];

			if (r.flags & kUnderwater)
				cont = true;

			if (HasVertex(r, x, y, z))
				return false;
		}

		if (!cont)
			return false;

		waterNeighbour = true;

		for (const Portal& p : cur.portals)
		{
			if (!Valid(p.rn))
				continue;

			for (const Portal& np : g_rooms[(size_t)p.rn].portals)
			{
				if (!Valid(np.rn))
					continue;

				const Room& nr = g_rooms[(size_t)np.rn];

				if ((nr.flags & kUnderwater) && HasVertex(nr, x, y, z))
					return false;
			}
		}

		return true;
	}

	// IsShoreVert: a vertex of an underwater room through a level portal.
	bool Shore(const Room& cur, const float* v)
	{
		if (!cur.door)
			return false;

		const float x = v[0] + cur.x, y = v[1], z = v[2] + cur.z;

		for (const Portal& p : cur.portals)
		{
			if (!Valid(p.rn) ||
				!(g_rooms[(size_t)p.rn].flags & kUnderwater) ||
				!p.normal[1] || Outside(p, v))
				continue;

			if (HasVertex(g_rooms[(size_t)p.rn], x, y, z))
				return true;
		}

		return false;
	}

	// aRoomVertexWaterMarks.
	unsigned char Bits(const Room& r, const Roomlet& l, long n)
	{
		const float* v = &r.vtx[l.first + (size_t)n * 7];
		unsigned char bits = 0;

		if (n < l.nWVtx)
			bits |= 1;

		if ((r.flags & kReflect) && (r.flags & kReflectCeiling))
		{
			if (!Mist(r, v) || std::fabs(v[1] - (float)r.minfloor) > 1536)
				bits |= 4;		}
		else if (r.flags & kMist)
		{
			if (Mist(r, v))
				bits |= 2;
		}
		else if (r.flags & kReflect)
		{
			bool waterNeighbour = false;
			const bool reflection = Reflection(r, v, waterNeighbour);

			if (reflection || Shore(r, v) || waterNeighbour)
				bits |= 2;
		}

		return bits;
	}

	template <typename T> T Read(const std::vector<uint8_t>& b, size_t at)
	{
		T value = T();

		if (at + sizeof(T) <= b.size())
			std::memcpy(&value, &b[at], sizeof(T));

		return value;
	}

	/* SCDS (game\init.cpp): the last portal of room rn whose outline,
	 * rounded to the quarter sector, holds the point ox, oz half a sector
	 * past the door; dx and dy in units, -1 for none. */
	int DoorPortal(int rn, long dx, long dy, long ox, long oz)
	{
		if (!Valid(rn) || !g_rooms[(size_t)rn].door)
			return -1;

		const Room& r = g_rooms[(size_t)rn];
		int found = -1;

		for (size_t k = 0; k < r.portals.size(); k++)
		{
			const Portal& p = r.portals[k];
			long minx = r.x + (dy >> 1) + ((p.v1[0] + 128) & ~255L);
			long maxx = r.x + (dy >> 1) + ((p.v3[0] + 128) & ~255L);
			long minz = r.z + (dx >> 1) + ((p.v1[2] + 128) & ~255L);
			long maxz = r.z + (dx >> 1) + ((p.v3[2] + 128) & ~255L);

			if (minx > maxx)
				std::swap(minx, maxx);

			if (minz > maxz)
				std::swap(minz, maxz);

			if (ox >= minx && ox <= maxx && oz >= minz && oz <= maxz)
				found = (int)k;
		}

		return found;
	}

	/* The closed doors as the level load leaves them, before the HD build
	 * asks for the marks: InitialiseDoor (game\init.cpp) finds for every
	 * CLOSED_DOOR1..LIFT_DOORS2 item the portal of its room, of the room
	 * behind it (GetDoor) and of their flip alternates (SetupClosedDoorStuff)
	 * and writes zero into their normals, as ShutThatDoor does. A shut
	 * portal is no downward portal to IsMistVert: ANDREA2's door in the
	 * floor of room 70 over room 204 leaves the 40 corners of that floor
	 * without a twin below, and the game gives them no mark. Returns the
	 * number of portals shut. */
	int ShutClosedDoors(const Tr5Level& level)
	{
		std::string error;
		int shut = 0;

		for (int i = 0; i < level.itemCount(); i++)
		{
			Tr5Item item;

			if (!level.getItem(i, item) ||
				item.objectNumber < walkoff::kClosedDoor1 ||
				item.objectNumber > walkoff::kLiftDoors2 ||
				!Valid(item.roomNumber))
				continue;

			const Room& own = g_rooms[(size_t)item.roomNumber];
			long dx = 0, dy = 0;

			if (!item.yRot)
				dx = -1;
			else if (item.yRot == -0x8000)
				dx = 1;
			else if (item.yRot == 0x4000)
				dy = -1;
			else
				dy = 1;

			const long sx = ((item.x - own.x) >> 10) + dy;
			const long sz = ((item.z - own.z) >> 10) + dx;
			std::vector<Tr5Level::SectorInfo> sectors;
			int behind = -1;

			if (level.sectorInfo(item.roomNumber, sectors, error))
				for (const Tr5Level::SectorInfo& s : sectors)
					if (s.sx == sx && s.sz == sz)
						behind = s.doorTo;

			if (!Valid(behind))
				continue;

			const long ox = item.x + (dy << 10);
			const long oz = item.z + (dx << 10);
			const int rooms[4] = { item.roomNumber, own.flipped, behind,
				g_rooms[(size_t)behind].flipped };

			for (int k = 0; k < 4; k++)
			{
				const int at = DoorPortal(rooms[k], dx << 10, dy << 10, ox,
					oz);

				if (at < 0)
					continue;

				Portal& p = g_rooms[(size_t)rooms[k]].portals[(size_t)at];

				p.normal[0] = p.normal[1] = p.normal[2] = 0;
				shut++;
			}
		}

		return shut;
	}

	/* The rooms out of the level, the closed doors shut, then
	 * BuildWaterMarks: every vertex of a room with ROOM_MIST or
	 * ROOM_REFLECT, the water vertices (nWVtx) of every other room. Returns
	 * the number of rooms with a mark. */
	int Build(const Tr5Level& level)
	{
		const std::vector<uint8_t>& b = level.debugBytes();
		const int count = level.roomCount() < kMaxRooms ?
			level.roomCount() : kMaxRooms;

		g_rooms.assign((size_t)count, Room());
		g_marks.clear();

		for (int i = 0; i < count; i++)
		{
			const size_t hdr = level.rooms()[(size_t)i].headerOffset;
			Room& r = g_rooms[(size_t)i];

			if (!hdr || hdr + 208 > b.size())
				continue;

			const size_t body = hdr + 208;
			const uint32_t door = Read<uint32_t>(b, hdr + 4);
			const int32_t lets = Read<int32_t>(b, hdr + 168);
			const size_t letAt = body + Read<uint32_t>(b, hdr + 172);
			size_t vtxAt = body + Read<uint32_t>(b, hdr + 176);

			r.x = Read<int32_t>(b, hdr + 20);
			r.z = Read<int32_t>(b, hdr + 28);
			r.minfloor = Read<int32_t>(b, hdr + 32);
			r.flipped = Read<int16_t>(b, hdr + 76);
			r.flags = Read<uint16_t>(b, hdr + 78);

			if (!(door & 1) && body + door + 2 <= b.size())
			{
				const short n = Read<short>(b, body + door);

				r.door = true;

				for (short k = 0; k < n; k++)
				{
					const size_t at = body + door + 2 + (size_t)k * 32;

					if (at + 32 > b.size())
						break;

					Portal p;

					std::memcpy(&p, &b[at], 32);
					r.portals.push_back(p);
				}
			}

			for (int32_t k = 0; k < lets; k++)
			{
				const size_t at = letAt + (size_t)k * 56;
				Roomlet l;

				l.nVtx = Read<short>(b, at);
				l.nWVtx = Read<short>(b, at + 2);
				l.first = r.vtx.size();

				if (at + 56 > b.size() || l.nVtx < 0 ||
					vtxAt + (size_t)l.nVtx * 28 > b.size())
					break;

				r.vtx.resize(r.vtx.size() + (size_t)l.nVtx * 7);

				if (l.nVtx)
					std::memcpy(&r.vtx[l.first], &b[vtxAt],
						(size_t)l.nVtx * 28);

				vtxAt += (size_t)l.nVtx * 28;
				r.roomlets.push_back(l);
			}
		}

		g_shut = ShutClosedDoors(level);

		int rooms = 0;

		for (int i = 0; i < count; i++)
		{
			const Room& r = g_rooms[(size_t)i];
			const bool interesting = (r.flags & (kMist | kReflect)) != 0;
			int here = 0;

			for (const Roomlet& l : r.roomlets)
			{
				const long last = interesting ? l.nVtx : l.nWVtx;

				for (long n = 0; n < last && n < l.nVtx; n++)
				{
					const unsigned char bits = Bits(r, l, n);

					if (!bits)
						continue;

					const float* v = &r.vtx[l.first + (size_t)n * 7];
					HDWaterMark mark;

					mark.room = (short)i;
					mark.x = (short)v[0];
					mark.y = (short)v[1];
					mark.z = (short)v[2];
					mark.bits = bits;
					g_marks.push_back(mark);
					here++;
				}
			}

			if (here)
				rooms++;
		}

		return rooms;
	}
}

/* The HD/SD switch in the tool. Its state moves by the engine's own rule
 * (hd_switch.h, HDSwitchStep) from HD_SWITCH_START, drawn HD, and HDDrawn()
 * answers from it; in the game the state is the setting hd_enable, read by
 * the engine's HDDrawn in hd_switch.cpp, which needs the game and is not
 * linked here. The gates that ask it in the game (hd_level.cpp,
 * hd_object.cpp, hd_laradraw.cpp, hd_life.cpp) are not linked either, so
 * the tool asks HDDrawn() once, where its frame's rooms are listed. The
 * tool builds the level HD in every run and links no build gate (HDEnabled,
 * hd_level.cpp), so its classic views (-classicroom with -classiconly) need
 * no switch for hd_build: their faces come from the .TRC. */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_switch.h"

static HDSwitchState g_hd_switch = HD_SWITCH_START;

bool HDDrawn()
{
	return g_hd_switch.drawn != 0;
}

/* -hddrawn 0|1: the switch pressed until HDDrawn() answers 1 (the default,
 * the picture as ever) or 0. A press is a key-down edge read live with HD
 * data, then the key up again, so 0 is one press from the start. While
 * HDDrawn() answers false the frame draws, in place of the art, the
 * classic faces of every room it lists - the seed room and every -with
 * room -, as InsertRoom draws a room's roomlets when its HD question
 * answers false: -classicroom for each, with -classiconly, after the whole
 * HD build has run. The tool's classic faces are read fresh from the .TRC
 * in -roombox and drawn flat grey, so this picture is the one the explicit
 * list gives; what the game's classic frame draws after an HD load is read
 * from the engine's code. A branch of its own in the option chain would
 * pass the compiler's nesting limit (C1061), so the chain's last else calls
 * this, and this reports any other argument as unknown. */
static void HDDrawnSwitch(int argc, char** argv, int& at)
{
	if (strcmp(argv[at], "-hddrawn") || at + 1 >= argc)
	{
		printf("RoomProbe: ignoring unknown argument \"%s\"\n", argv[at]);
		return;
	}

	const int drawn = atoi(argv[++at]) ? 1 : 0;

	if (drawn != (HDDrawn() ? 1 : 0))
	{
		HDSwitchStep(&g_hd_switch, 1, 1, 1);
		HDSwitchStep(&g_hd_switch, 0, 1, 1);
	}

	printf("hddrawn: HDDrawn() %d\n", HDDrawn() ? 1 : 0);

	if (HDDrawn())
		return;

	std::vector<int> want(1, atoi(argv[3]));

	for (int k = 4; k + 1 < argc; k++)
		if (!strcmp(argv[k], "-with"))
			want.push_back(atoi(argv[k + 1]));

	// A room named twice, or by -classicroom itself, is drawn once.
	for (size_t w = 0; w < want.size(); w++)
	{
		bool named = false;

		for (size_t v = 0; v < w; v++)
			named = named || want[v] == want[w];

		for (int k = 4; k + 1 < argc; k++)
			named = named || ((!strcmp(argv[k], "-classicroom") ||
				!strcmp(argv[k], "-classicmark")) &&
				atoi(argv[k + 1]) == want[w]);

		if (!named)
			g_classic_rooms.push_back(want[w]);
	}

	g_classic_only = true;
	printf("hddrawn: 0 - the classic faces of %d room(s) in place of the art\n",
		(int)want.size());
}

/* -loadtime: the level's HD build timed step by step, one line per step in
 * the form of the game's LOADLOG lines (specific\file.cpp,
 * specific\hd\hd_level.cpp), so that a level's steps on this machine can be
 * held against a game log's. The steps before the build are this tool's
 * twins of hd_level.cpp's (they need -roombox); the build's own parts come
 * out of HDRenderStats exactly as the game's built line takes them. The
 * .TRG scan of the game's load thread runs too, with the level's slots out
 * of the classic file. What the tool does not run: the classic load, aInit,
 * the static tables, the water's steps, the objects and the font. A
 * -loadtime run takes no picture. */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_bind.h"

static bool g_loadtime = false;
static double g_loadtime_at = 0.0;

/* The engine's read ahead (hd_level.cpp, HDLevelKeyNext): the .TRG read and
 * the first pages decoded on a thread of its own from the start of the load,
 * beside this tool's twins of the steps before the scan. */
static double g_loadtime_read_ms = 0.0;

// Joined at the latest at the end: a joinable std::thread would abort it.
static struct LoadTimeAhead
{
	std::thread t;

	~LoadTimeAhead()
	{
		if (t.joinable())
			t.join();
	}
} g_loadtime_ahead;
/* -ahead: the same read ahead, scan and pages ahead in a run that goes on to
 * its pictures, which must equal a run without; -aheadverify decodes every
 * page the build takes again and counts those that differ. */
static bool g_ahead_pics = false;
static bool g_ahead_verify = false;

static void LoadTimeAheadStart(const char* root, const char* stem, int threads)
{
	const std::string r = root, s = stem;

	g_loadtime_ahead.t = std::thread([r, s, threads]()
	{
		LARGE_INTEGER a, b, rate;

		QueryPerformanceCounter(&a);

		const bool read = HDScanReadTrg(r.c_str(), s.c_str());

		QueryPerformanceCounter(&b);
		QueryPerformanceFrequency(&rate);
		g_loadtime_read_ms = (double)(b.QuadPart - a.QuadPart) * 1000.0 /
			(double)rate.QuadPart;

		if (!read)
			return;

		std::vector<int> pages((size_t)HDScanKeptPageCount());

		for (int i = 0; i < (int)pages.size(); i++)
			pages[(size_t)i] = HDScanKeptPage(i);

		HDPageCacheAhead(r.c_str(), pages.empty() ? 0 : &pages[0],
			(int)pages.size(), threads);
	});
}

// What the scan waited for the read, in milliseconds.
static double LoadTimeAheadWait()
{
	LARGE_INTEGER a, b, rate;

	QueryPerformanceCounter(&a);

	if (g_loadtime_ahead.t.joinable())
		g_loadtime_ahead.t.join();

	QueryPerformanceCounter(&b);
	QueryPerformanceFrequency(&rate);
	return (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)rate.QuadPart;
}

static void LoadTimeStep(const char* stem, const char* step)
{
	if (!g_loadtime)
		return;

	LARGE_INTEGER now, rate;

	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&rate);

	const double ms = (double)now.QuadPart * 1000.0 / (double)rate.QuadPart;

	if (step)
		printf("LOADLOG level=%s step=%s ms=%.1f\n", stem, step,
			ms - g_loadtime_at);

	g_loadtime_at = ms;
}

// The game's HDScanLevel call (HDLevelLoad): pages not decoded, one thread.
static void LoadTimeScan(const char* root, const char* stem,
	const Tr5Level& level)
{
	if (!g_loadtime && !g_ahead_pics)
		return;

	std::vector<unsigned char> loaded(HD_SLOT_COUNT, 0);
	HDScanStats scan;
	const char* why = "";

	for (int slot : level.moveableSlots())
		if (slot >= 0 && slot < HD_SLOT_COUNT)
			loaded[(size_t)slot] = 1;

	// The read ahead waited for and the parse kept, as HDLevelLoad does.
	const double waited = LoadTimeAheadWait();

	if (g_loadtime)
		printf("LOADLOG level=%s step=trg_ahead ms=%.1f waited=%.1f\n", stem,
			g_loadtime_read_ms, waited);

	LoadTimeStep(stem, 0);
	HDScanKeepTrg(true);

	if (!HDScanLevel(root, stem, level.roomCount(), &loaded[0], HD_SLOT_COUNT,
		false, 1, &scan, &why))
		printf("LOADLOG level=%s scan failed: %s\n", stem, why);

	LoadTimeStep(stem, "scan");
}

// The build's own parts, in the order and under the names of hd_level.cpp.
static void LoadTimeBuild(const char* stem, const HDRenderStats& stats)
{
	if (!g_loadtime)
		return;

	printf("LOADLOG level=%s step=trg_read ms=%.1f\n", stem, stats.readMs);

	for (int b = 0; b < HD_BEGIN_PHASES; b++)
		printf("LOADLOG level=%s step=%s ms=%.1f\n", stem,
			HDRenderBeginPhaseName(b), stats.phaseMs[b]);

	printf("LOADLOG level=%s step=hash ms=%.1f\n", stem, stats.hashMs);
	int ahead = 0, taken = 0;
	double ahead_ms = 0.0;

	HDPageCacheAheadInfo(&ahead, &taken, &ahead_ms);
	printf("LOADLOG level=%s step=begin ms=%.1f pages_read=%.1f "
		"pages_decode=%.1f pages_upload=%.1f threads=%d trg_kept=%d "
		"hash=%016llx ahead=%d taken=%d ahead_ms=%.1f peak_mib=%.1f\n", stem,
		stats.readMs + stats.buildMs, stats.pages.readMs,
		stats.pages.decodeMs, stats.pages.uploadMs, stats.pages.threads,
		HDScanTrgTaken() ? 1 : 0, stats.hash, ahead, taken, ahead_ms,
		PeakCommitMiB());
}

/* -reload: the HD side of a savegame loaded inside the level, as far as this
 * tool follows the engine (hd_level.cpp, HDLevelUnload and
 * HDLevelBuildResident). After the build the level counts as read again: the
 * key of the level about to be read is held against the key of the build
 * with the engine's own comparison (hd_levelkey.h). With the same key
 * nothing of the build is released or made again; the water sheets (-hdwater)
 * are set a second time over it, with a ring left from the first, as the
 * resident build sets them; the walk applies -flip to the rooms it reads
 * from the file. So the room art is equal by construction, as the engine's
 * keep makes it; what the run tests is the sheets set again and the walk.
 * -reloadas <LEVEL> names another level: the key differs, this level's
 * build is shut down and the run is made again for that level in the same
 * process (ProbeReloadOther), with its own room inputs out of -roombox as
 * any run hands them in; it prints that build's fingerprint and draws
 * nothing. With -ahead the other level's .TRG is read and its first pages
 * decoded while this level's build is still in memory, the engine's order
 * at a level change, and the commit is printed at each step. Not followed:
 * the classic load into the game's pool, FreeLevel, the objects' rebind,
 * the water's life, the re-dress rule and sgRestoreGame's flips - the tool
 * has none of them - and the device, which the engine keeps across levels
 * and the tool makes again. */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_levelkey.h"

static bool g_reload = false;
static std::string g_reload_as;
static bool g_reload_kept = false;
// The key differed: main runs the other level's build after this run.
static bool g_reload_refused = false;
// This run is that build: its fingerprint, then the end of the run.
static bool g_reload_rebuild = false;

static void ProbeFileStamp(const std::string& path, HDLevelFileStamp* out)
{
	WIN32_FILE_ATTRIBUTE_DATA a;

	out->size = -1;
	out->written = -1;

	if (path.empty() || !GetFileAttributesExA(path.c_str(),
		GetFileExInfoStandard, &a) || (a.dwFileAttributes &
		FILE_ATTRIBUTE_DIRECTORY))
		return;

	out->size = ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
	out->written = ((long long)a.ftLastWriteTime.dwHighDateTime << 32) |
		a.ftLastWriteTime.dwLowDateTime;
}

// The engine's key (HDLevelKeyNext) out of this tool's files and switches.
static void ProbeLevelKey(const char* root, const char* stem, double budgetMiB,
	int pageFormat, HDLevelKey* k)
{
	memset(k, 0, sizeof(*k));
	k->valid = 1;
	snprintf(k->name, sizeof(k->name), "%s", stem);

	const std::string classic = g_roombox.empty() ? std::string()
		: g_roombox + "\\" + stem;

	ProbeFileStamp(classic.empty() ? classic : classic + ".TRC", &k->trc);
	ProbeFileStamp(classic.empty() ? classic : classic + ".hdm", &k->hdm);
	ProbeFileStamp(std::string(root) + "\\DATA\\" + stem + ".TRG", &k->trg);
	ProbeFileStamp(g_gloss_file, &k->glossFile);
	ProbeFileStamp(g_water_file, &k->waterFile);
	snprintf(k->install, sizeof(k->install), "%s", root);
	k->air = g_air_option;
	k->water = g_hdwater == 1 ? 1 : 0;
	k->budgetMb = (long)budgetMiB;
	snprintf(k->format, sizeof(k->format), "%s",
		pageFormat == HD_PAGE_FORMAT_DXT5 ? "dxt5" : "rgba");
}

/* True when the run goes on to its pictures over the kept build, false when
 * the key differed and the other level was built in its place. */
static bool ProbeReload(const char* root, const char* stem, int rooms,
	bool managed, int threads, double budgetMiB, int pageFormat)
{
	const char* as = g_reload_as.empty() ? stem : g_reload_as.c_str();
	HDLevelKey built, next;

	ProbeLevelKey(root, stem, budgetMiB, pageFormat, &built);
	ProbeLevelKey(root, as, budgetMiB, pageFormat, &next);

	const char* refused = HDLevelKeepRefused(&built, &next, 0);

	printf("-reload: level=%s resident=%d%s%s\n", as, refused ? 0 : 1,
		refused ? " refused=" : "", refused ? refused : "");

	if (!refused)
	{
		g_reload_kept = true;
		return true;
	}

	g_reload_refused = true;

	/* The room lists the build borrowed, given back as hd_level.cpp gives
	 * them back after every build: a list the next level's run does not
	 * hand in would otherwise still name this level's. */
	HDRenderSetWaterMarks(0, 0);
	HDRenderSetRoomBoxes(0, 0);
	HDRenderSetSectors(0, 0, 0, 0);
	HDRenderSetStandFaces(0, 0, 0, 0, 0, 0);
	HDRenderSetIntrusions(0, 0);
	HDRenderSetPairFaces(0, 0);
	HDRenderSetRoomAir(0, 0, 0);
	HDRenderSetCarryHosts(0, 0, 0);
	HDRenderSetCuts(0, 0);
	HDRenderSetStaticPlaces(0, 0);

	/* -ahead: what the engine holds at a level change before the release
	 * (hd_level.cpp, HDLevelKeyNext at the top of LoadLevel; the build
	 * before is released at the next build): this build, then beside it
	 * the other level's parse - all the scan held beside it at afbb6f6b -
	 * and then its pages decoded ahead, every one of them (the most they
	 * hold; the engine's build stops the decode where it starts). The
	 * other level's run takes both, as the engine's build does. */
	if (g_ahead_pics)
	{
		const double peak = PeakCommitMiB();
		const double alone = CommitMiB();
		const bool read = HDScanReadTrg(root, as);
		const double parse = CommitMiB();

		if (read)
		{
			std::vector<int> pages((size_t)HDScanKeptPageCount());

			for (int i = 0; i < (int)pages.size(); i++)
				pages[(size_t)i] = HDScanKeptPage(i);

			HDPageCacheAhead(root, pages.empty() ? 0 : &pages[0],
				(int)pages.size(), threads);
			HDPageCacheAheadFinish();
		}

		int ahead = 0;

		HDPageCacheAheadInfo(&ahead, 0, 0);
		printf("-reload: commit with the build of %s %.1f MiB (its peak "
			"%.1f), %s's .TRG read beside it %.1f (+%.1f), its %d page(s) "
			"decoded ahead too %.1f (+%.1f)\n", stem, alone, peak, as, parse,
			parse - alone, ahead, CommitMiB(), CommitMiB() - parse);
	}

	return false;
}

/* -loadtime -reload: the same level read again after the build, as LOADLOG
 * lines - the tool's classic read, the key and its answer, and every step
 * the engine's resident load does not run at 0 with skipped=1 (hd_level.cpp,
 * HDLevelLoad and HDLevelBuildResident). The engine's own steps of that
 * load (the sheets, the objects' rebind, the life, the re-dress) are not
 * in this tool. */
static void LoadTimeReload(const char* root, const char* stem,
	double budgetMiB, int pageFormat)
{
	LoadTimeStep(stem, 0);

	Tr5Level level;
	std::string error;
	const std::string trc = g_roombox + "\\" + stem + ".TRC";
	std::wstring wide(trc.begin(), trc.end());

	level.load(wide, error);
	LoadTimeStep(stem, "classic_read");

	HDLevelKey built, next;

	ProbeLevelKey(root, stem, budgetMiB, pageFormat, &built);
	ProbeLevelKey(root, stem, budgetMiB, pageFormat, &next);

	const char* refused = HDLevelKeepRefused(&built, &next, 0);

	LoadTimeStep(stem, "key");
	printf("LOADLOG level=%s step=resident ms=0.0 kept=%d%s%s\n", stem,
		refused ? 0 : 1, refused ? " refused=" : "", refused ? refused : "");

	static const char* const skipped[] =
	{
		"trg_ahead", "scan", "boxes", "sectors", "intr", "air", "pairs",
		"stand", "places", "begin", "trg_read", "prep", "pages", "owners",
		"verts", "setup", "fill", "index", "hash", "pages_ahead"
	};

	for (const char* step : skipped)
		if (!refused)
			printf("LOADLOG level=%s step=%s ms=0.0 skipped=1\n", stem, step);
}

/* What the springs' rule keeps clear of, out of the classic .TRC, as
 * hd_spring.cpp's LevelBlocks reads it out of the engine: every portal
 * through a wall (normal y 0) of every room, its corners from the room's
 * origin; the start of Lara's item (slot 0); every static (probestatics,
 * world x, z). And into `rooms` each room's box, as hd_spring.cpp's
 * LevelRooms reads it. */
static void SpringBlocks(const char* stem, std::vector<HDSpringBlock>& out,
	std::vector<HDSpringRoom>* rooms)
{
	Tr5Level level;
	std::string error;
	const std::string trc = g_roombox + "\\" + stem + ".TRC";
	std::wstring wide;

	for (size_t i = 0; i < trc.size(); i++)
		wide.push_back((wchar_t)(unsigned char)trc[i]);

	if (g_roombox.empty() || !level.load(wide, error))
	{
		printf("-springs: %s could not be read - no blocks\n", trc.c_str());
		return;
	}

	for (int r = 0; r < level.roomCount(); r++)
	{
		const Tr5Room& rm = level.rooms()[(size_t)r];
		std::vector<Tr5Level::PortalRecord> recs;
		unsigned yOrigin = 0;

		// The room's box for the cracks' floor (hd_spring.h, HDSpringRoom).
		if (rooms)
		{
			const HDSpringRoom q = { r, (float)rm.originX, (float)rm.originZ,
				(float)(rm.originX + rm.numXSectors * 1024),
				(float)(rm.originZ + rm.numZSectors * 1024) };

			rooms->push_back(q);
		}

		level.headerField(r, 24, yOrigin, error);

		if (!level.portalRecords(r, recs, error))
			continue;

		for (const Tr5Level::PortalRecord& p : recs)
		{
			if (p.ny)
				continue;

			HDSpringBlock b = { HD_SPRING_PORTAL, 1.0e9f, 1.0e9f, -1.0e9f,
				-1.0e9f, 1.0e9f, -1.0e9f };

			for (int k = 0; k < 4; k++)
			{
				const float x = (float)(rm.originX + p.v[k][0]);
				const float y = (float)((int)yOrigin + p.v[k][1]);
				const float z = (float)(rm.originZ + p.v[k][2]);

				b.x0 = fminf(b.x0, x);
				b.x1 = fmaxf(b.x1, x);
				b.y0 = fminf(b.y0, y);
				b.y1 = fmaxf(b.y1, y);
				b.z0 = fminf(b.z0, z);
				b.z1 = fmaxf(b.z1, z);
			}

			out.push_back(b);
		}
	}

	for (int i = 0; i < level.itemCount(); i++)
	{
		Tr5Item it;

		if (level.getItem(i, it) && it.objectNumber == 0)
		{
			const HDSpringBlock b = { HD_SPRING_START, (float)it.x,
				(float)it.z, (float)it.x, (float)it.z, -1.0e9f, 1.0e9f };

			out.push_back(b);
		}
	}

	if (probestatics::Load(stem))
		for (const probestatics::Static& s : probestatics::g_list)
		{
			const HDSpringBlock b = { HD_SPRING_STATIC, (float)s.x,
				(float)s.z, (float)s.x, (float)s.z, -1.0e9f, 1.0e9f };

			out.push_back(b);
		}
}

/* The visibility path's switches, out of ProbeMain's chain of else-ifs:
 *
 *   -vis 0|1          tomb5.hd_vis: 1 walks the portals' list extended by
 *                     the three tables (hd_vis.h) with the reach off, no
 *                     carried rooms, every group drawn whole but for its
 *                     pieces of intruding art, answered as -viswin says;
 *                     0, the default, changes nothing
 *   -vissrc <mask>    the tables -vis 1 asks: 1 guests, 2 openings, 4 wide
 *                     portals (7, the default, all three)
 *   -visreach <units> -visbare <share> -visguest <triangles>
 *   -visguestrule 0|1|2 -visoverhang <units>   the tables' numbers in place
 *                     of HDVisDefaults'
 *   -viswidebare <share>   the bare share that lets a face widen a portal
 *   -visedge <share>  the window's edge, as a share of the screen's width
 *   -viswin <rule>    how -vis 1 answers the pieces of intruding art (see
 *                     g_vis_win): 2, the default, as the engine; 0 every
 *                     piece whole
 *   -visportals <room>
 *                     that room's portal records at the walk's load: the
 *                     room behind, the normal and the world corners; -1
 *                     prints every room's
 *   -visplane 0|1     1, the default, keeps a piece in unlinked air whole
 *                     only on a drawn claimed face that is one wall with
 *                     its own face, in its plane or round a corner
 *                     (HDVisWallDrawn, DecidePairs); 0 on any claimed face
 *                     of its pair
 *   -visfaces 0|1     1 prints each piece's claimed face, its corners in
 *                     the seed room's frame, under its decide line
 *   -visnear 0|1      1, the default, lists the room behind a portal the
 *                     eye stands at (VisNearPortals); 0 leaves that out
 *   -gpu <n>          the frame's solid room draws timed n times on the GPU
 *                     after the picture (the print beside the draw calls) */
static bool VisArg(int argc, char** argv, int* at)
{
	const char* a = argv[*at];

	if (*at + 1 >= argc)
		return false;

	if (!strcmp(a, "-vis"))
	{
		walkoff::g_vis = atoi(argv[*at + 1]) == 1;
		g_vis_option = walkoff::g_vis;

		printf("-vis: %s\n", g_vis_option ? "the visibility path"
			: "the room-art rules (default)");
	}
	else if (!strcmp(a, "-vissrc"))
		walkoff::g_vis_sources = atoi(argv[*at + 1]) & 7;
	else if (!strcmp(a, "-visreach"))
		walkoff::g_vis_params.reach = atof(argv[*at + 1]);
	else if (!strcmp(a, "-visbare"))
		walkoff::g_vis_params.bare = atof(argv[*at + 1]);
	else if (!strcmp(a, "-visguest"))
		walkoff::g_vis_params.guestTris = atoi(argv[*at + 1]);
	else if (!strcmp(a, "-visguestrule"))
		walkoff::g_vis_params.guestRule = atoi(argv[*at + 1]);
	else if (!strcmp(a, "-visoverhang"))
		walkoff::g_vis_params.overhang = atof(argv[*at + 1]);
	else if (!strcmp(a, "-viswidebare"))
		walkoff::g_vis_params.wideBare = atof(argv[*at + 1]);
	else if (!strcmp(a, "-visedge"))
		walkoff::g_vis_params.edge = atof(argv[*at + 1]);
	else if (!strcmp(a, "-viswin"))
		g_vis_win = atoi(argv[*at + 1]);
	else if (!strcmp(a, "-visportals"))
		walkoff::g_portal_room = atoi(argv[*at + 1]);
	else if (!strcmp(a, "-visplane"))
		g_vis_plane = atoi(argv[*at + 1]) != 0;
	else if (!strcmp(a, "-visfaces"))
		g_vis_faces = atoi(argv[*at + 1]) != 0;
	else if (!strcmp(a, "-visnear"))
		walkoff::g_vis_near = atoi(argv[*at + 1]) != 0;
	else if (!strcmp(a, "-gpu"))
		g_gpu_runs = atoi(argv[*at + 1]);
	else
		return false;

	(*at)++;
	return true;
}

/* -skycensus: what the sky of one level is made of, read out of the two
 * files and printed, with no device and no picture. The .TRG's groups past
 * the last room are the HD sky; per part and pass a `sky part` line gives
 * the material and its four parameters, the triangles, the .TEX pages, the
 * height, the distance from the dome's axis and the elevation seen from its
 * centre, the u and v bytes, the baked light and the vertex numbers (the
 * .TRG writes a part's vertices as one block). A part whose vertices all lie
 * at one height is a flat sheet, and its line adds the UV bytes a thousand
 * units of it take (the page's tiling). Every height of the band gets a
 * `sky ring` line with its v bytes, which is how the page's rows lie on the
 * sphere. The classic HORIZON mesh (slot 454) of the .TRC under -roombox
 * gets the same rings, with the classic page and the texinfo v of the faces
 * that touch each ring. -skydump <path> writes every sky triangle as one
 * line: group part pass page, then x y z u v of its three corners. */
static bool g_sky_census = false;
static std::string g_sky_dump;

/* -skylayer <1|2> <r> <g> <b> <vel>: one of the script's classic flat layers
 * drawn over the HD sky the way SkyDrawPhase draws it (HDRenderSkyLayerProbe),
 * its counter where UpdateSky leaves it after -time seconds of logic ticks
 * (30 a second, `vel` a tick, wrapped at 0x2600). Repeatable for the two.
 * -skylayerpage sd|hd: the page it carries, the level's classic sky page out
 * of the .TRC under -roombox (sd, the default) or the HD page the game
 * borrows for it (hd), or the PSX sky's tiles (psx): DrawSkySegment's 4 x 4
 * tiles a quad on the four SKY_GRAPHICS sprites' windows and page, as the
 * game draws the layers with PSX_skies on and no HD page lent. */
struct SkyLayerArg
{
	int layer;
	unsigned long colour;
	int vel;
};
static std::vector<SkyLayerArg> g_sky_layers;
static bool g_sky_layer_hd = false;
static bool g_sky_layer_psx = false;
static float g_sky_psx_windows[16];

/* -skycloud <floor> <thickness> <cell> <coverage> <density> <vel>: the
 * volumetric cloud layer over the HD sky and the layers, a measurement only
 * (HDRenderSkyCloudProbe): a slab from `floor` to `floor + thickness` units
 * over the camera, a lattice of `cell` units, the air clear below
 * `coverage`, `density` per unit path, carried along x as UpdateSky carries
 * a layer moving `vel` a tick (30 ticks a second, over -time). The colours
 * are a free choice for the measurement: 0.92 0.93 0.97 at the top, 0.42
 * 0.44 0.52 at the floor; the fade towards the horizon 0.15 of the rise.
 * -skygpu <n>: after the picture, the HD sky, the layers and the cloud
 * layer each drawn n times more in a GPU timestamp bracket of its own; one
 * `sky gpu` line each with the median and the spread in ms. */
static bool g_sky_cloud = false;
static float g_sky_cloud_args[6] = { 0, 0, 0, 0, 0, 0 };
static int g_sky_gpu = 0;

/* The sky's switches, ahead of the parse chain, which stands at the
 * compiler's limit of nested blocks (C1061). */
static bool SkyArg(int argc, char** argv, int* at)
{
	const char* a = argv[*at];

	if (!strcmp(a, "-skycensus"))
		g_sky_census = true;
	else if (!strcmp(a, "-skydump") && *at + 1 < argc)
		g_sky_dump = argv[++*at];
	else if (!strcmp(a, "-skylayer") && *at + 5 < argc)
	{
		SkyLayerArg l;

		l.layer = atoi(argv[*at + 1]);
		l.colour = (unsigned long)(atoi(argv[*at + 2]) & 0xFF) << 16 |
			(unsigned long)(atoi(argv[*at + 3]) & 0xFF) << 8 |
			(unsigned long)(atoi(argv[*at + 4]) & 0xFF);
		l.vel = atoi(argv[*at + 5]);
		g_sky_layers.push_back(l);
		*at += 5;
	}
	else if (!strcmp(a, "-skylayerpage") && *at + 1 < argc)
	{
		g_sky_layer_hd = !strcmp(argv[*at + 1], "hd");
		g_sky_layer_psx = !strcmp(argv[++*at], "psx");
	}
	else if (!strcmp(a, "-skycloud") && *at + 6 < argc)
	{
		for (int i = 0; i < 6; i++)
			g_sky_cloud_args[i] = (float)atof(argv[*at + 1 + i]);

		g_sky_cloud = true;
		*at += 6;
	}
	else if (!strcmp(a, "-skygpu") && *at + 1 < argc)
		g_sky_gpu = atoi(argv[++*at]);
	else
		return false;

	return true;
}

/* The level's classic sky page: the last 256 x 256 block of the .TRC's misc
 * texture block (shine, font, sky - LoadTexturesMisc), after the 32 and 16
 * bit page blocks; the header before them is the version and three page
 * counts (10 bytes). A MANAGED A8R8G8B8 texture, or 0. */
static IDirect3DTexture9* SkyClassicPage(const char* stem)
{
	const std::string trc = g_roombox + "\\" + stem + ".TRC";
	FILE* f = 0;

	if (g_roombox.empty() || fopen_s(&f, trc.c_str(), "rb") || !f)
		return 0;

	std::vector<unsigned char> sky;
	unsigned int sizes[2] = { 0, 0 };

	fseek(f, 10, SEEK_SET);

	for (int block = 0; block < 3; block++)
	{
		if (fread(sizes, 4, 2, f) != 2)
			break;

		if (block < 2)
		{
			fseek(f, (long)sizes[1], SEEK_CUR);
			continue;
		}

		std::vector<unsigned char> packed(sizes[1]), plain(sizes[0]);
		uLongf length = sizes[0];

		if (sizes[0] >= 0x40000 && fread(&packed[0], 1, sizes[1], f) ==
			sizes[1] && uncompress(&plain[0], &length, &packed[0],
			sizes[1]) == Z_OK && length == sizes[0])
			sky.assign(plain.end() - 0x40000, plain.end());
	}

	fclose(f);

	IDirect3DTexture9* page = 0;
	D3DLOCKED_RECT rect;

	if (sky.empty() || FAILED(g_device->CreateTexture(256, 256, 1, 0,
		D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &page, 0)))
		return 0;

	if (FAILED(page->LockRect(0, &rect, 0, 0)))
	{
		page->Release();
		return 0;
	}

	for (int y = 0; y < 256; y++)
		memcpy((BYTE*)rect.pBits + y * rect.Pitch, &sky[(size_t)y * 1024],
			1024);

	page->UnlockRect(0);
	return page;
}

/* The PSX sky's page (DrawSkySegment): the four SKY_GRAPHICS sprites of the
 * .TRC under -roombox, found as LoadSprites reads them after the "SPR"
 * marker, and the 32 bit object page they share, out of the first texture
 * block. `windows` gets x1 y1 x2 y2 per sprite in page units with
 * LoadSprites' one texel trimmed off each side. A MANAGED A8R8G8B8 texture,
 * or 0 when a sprite is missing or the four lie on different pages. */
static IDirect3DTexture9* SkyPsxPage(const char* stem, float* windows)
{
	const std::string trc = g_roombox + "\\" + stem + ".TRC";
	FILE* f = 0;

	if (g_roombox.empty() || fopen_s(&f, trc.c_str(), "rb") || !f)
		return 0;

	std::vector<unsigned char> file;

	fseek(f, 0, SEEK_END);
	file.resize((size_t)ftell(f));
	fseek(f, 0, SEEK_SET);

	const bool read = !file.empty() &&
		fread(&file[0], 1, file.size(), f) == file.size();

	fclose(f);

	if (!read || file.size() < 18)
		return 0;

	unsigned int sizes[2];

	memcpy(sizes, &file[10], 8);

	std::vector<unsigned char> tex32(sizes[0]);
	uLongf length = sizes[0];

	if (18 + (size_t)sizes[1] > file.size() || uncompress(&tex32[0], &length,
		&file[18], sizes[1]) != Z_OK || length != sizes[0])
		return 0;

	// The marker followed by a sane sprite count, as LoadSprites finds it.
	size_t at = 0;
	int count = -1;

	for (size_t i = 0; i + 8 <= file.size(); i++)
		if (!memcmp(&file[i], "SPR", 4))
		{
			memcpy(&count, &file[i + 4], 4);

			if (count >= 0 && count < 2000)
			{
				at = i + 8;
				break;
			}

			count = -1;
		}

	if (count < 0)
		return 0;

	const size_t slotsAt = at + 16 * (size_t)count;
	int slots = 0, first = -1;

	if (slotsAt + 4 > file.size())
		return 0;

	memcpy(&slots, &file[slotsAt], 4);

	for (int i = 0; i < slots && slotsAt + 12 + 8 * (size_t)i <= file.size();
		i++)
	{
		int slot;
		short n, offset;

		memcpy(&slot, &file[slotsAt + 4 + 8 * (size_t)i], 4);
		memcpy(&n, &file[slotsAt + 8 + 8 * (size_t)i], 2);
		memcpy(&offset, &file[slotsAt + 10 + 8 * (size_t)i], 2);

		if (slot == 455 && n <= -4)	// SKY_GRAPHICS, game/objects.h
			first = offset;
	}

	if (first < 0 || first + 4 > count)
		return 0;

	int tpage = -1;

	for (int d = 0; d < 4; d++)
	{
		unsigned short head[4];
		short box[4];
		const size_t s = at + 16 * (size_t)(first + d);

		memcpy(head, &file[s], 8);
		memcpy(box, &file[s + 8], 8);

		if (tpage >= 0 && head[0] != tpage)
			return 0;

		tpage = head[0];
		windows[4 * d + 0] = (float)box[0] / 256.0f + 1.0f / 256.0f;
		windows[4 * d + 1] = (float)box[1] / 256.0f + 1.0f / 256.0f;
		windows[4 * d + 2] = (float)box[2] / 256.0f - 1.0f / 256.0f;
		windows[4 * d + 3] = (float)box[3] / 256.0f - 1.0f / 256.0f;
	}

	if ((size_t)(tpage + 1) * 0x40000 > tex32.size())
		return 0;

	IDirect3DTexture9* page = 0;
	D3DLOCKED_RECT rect;

	if (FAILED(g_device->CreateTexture(256, 256, 1, 0, D3DFMT_A8R8G8B8,
		D3DPOOL_MANAGED, &page, 0)))
		return 0;

	if (FAILED(page->LockRect(0, &rect, 0, 0)))
	{
		page->Release();
		return 0;
	}

	for (int y = 0; y < 256; y++)
		memcpy((BYTE*)rect.pBits + y * rect.Pitch,
			&tex32[(size_t)tpage * 0x40000 + (size_t)y * 1024], 1024);

	page->UnlockRect(0);
	printf("sky psx: SKY_GRAPHICS sprites %d..%d on page %d\n", first,
		first + 3, tpage);
	return page;
}

// A timestamp bracket on the GPU: Begin, then End returns its ms (-1).
struct SkyTimer
{
	IDirect3DQuery9* q[4] = { 0, 0, 0, 0 };

	void Begin()
	{
		g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &q[0]);
		g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1]);
		g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2]);
		g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &q[3]);

		if (q[0] && q[1] && q[2] && q[3])
		{
			q[0]->Issue(D3DISSUE_BEGIN);
			q[1]->Issue(D3DISSUE_END);
		}
	}

	double End()
	{
		double ms = -1.0;

		if (q[0] && q[1] && q[2] && q[3])
		{
			UINT64 a = 0, b = 0, f = 0;
			BOOL disjoint = TRUE;

			q[2]->Issue(D3DISSUE_END);
			q[0]->Issue(D3DISSUE_END);
			q[3]->Issue(D3DISSUE_END);

			while (q[0]->GetData(&disjoint, sizeof(disjoint),
				D3DGETDATA_FLUSH) == S_FALSE)
				;

			while (q[1]->GetData(&a, sizeof(a), D3DGETDATA_FLUSH) == S_FALSE)
				;

			while (q[2]->GetData(&b, sizeof(b), D3DGETDATA_FLUSH) == S_FALSE)
				;

			while (q[3]->GetData(&f, sizeof(f), D3DGETDATA_FLUSH) == S_FALSE)
				;

			if (!disjoint && f)
				ms = (double)(b - a) * 1000.0 / (double)f;
		}

		for (int i = 0; i < 4; i++)
			if (q[i])
			{
				q[i]->Release();
				q[i] = 0;
			}

		return ms;
	}
};

static void ProbeSkyCloud(const D3DMATRIX* world)
{
	if (!g_sky_cloud)
		return;

	const float* a = g_sky_cloud_args;
	const double span = 64.0 * (double)a[2];
	double wind = fmod((double)a[5] * floor(g_time * 30.0), span);

	if (wind < 0.0)
		wind += span;

	const float slab[4] = { a[0], a[1], (float)wind, a[2] };
	const float shape[4] = { a[3], a[4], 0.15f, 0.0f };
	const float top[3] = { 0.92f, 0.93f, 0.97f };
	const float bottom[3] = { 0.42f, 0.44f, 0.52f };

	HDRenderSkyCloudProbe(world, slab, shape, top, bottom);
}

/* The layers of -skylayer over the HD sky just drawn, in SkyDrawPhase's
 * order: layer 1, then layer 2; then -skycloud's layer. Called right after
 * HDRenderSky. */
static void ProbeSkyLayers(const D3DMATRIX* world, const char* stem)
{
	IDirect3DTexture9* page = 0;

	if (!g_sky_layers.empty())
	{
		page = g_sky_layer_hd ? HDRenderSkyLayerPage() : g_sky_layer_psx ?
			SkyPsxPage(stem, g_sky_psx_windows) : SkyClassicPage(stem);
		printf("sky layers: %d on the %s page%s\n",
			(int)g_sky_layers.size(), g_sky_layer_hd ? "HD" :
			g_sky_layer_psx ? "PSX sprites'" : "classic",
			page ? "" : " - NONE, not drawn");
	}

	for (int pass = 1; pass <= 2 && page; pass++)
		for (const SkyLayerArg& l : g_sky_layers)
		{
			if (l.layer != pass)
				continue;

			const long ticks = (long)floor(g_time * 30.0);
			long pos = (long)(((long long)l.vel * ticks) % 0x2600);

			if (pos < 0)
				pos += 0x2600;

			HDRenderSkyLayerProbe(world, page, l.colour, (float)pos, l.layer,
				g_sky_layer_psx ? g_sky_psx_windows : 0);
		}

	ProbeSkyCloud(world);

	/* -skygpu: each part n times in its own bracket. They draw into the
	 * picture's own target, so a -skygpu run is for its numbers and not for
	 * its picture. */
	for (int part = 0; part < 3 && g_sky_gpu > 0; part++)
	{
		if ((part == 1 && !page) || (part == 2 && !g_sky_cloud))
			continue;

		std::vector<double> ms;

		for (int run = 0; run < g_sky_gpu; run++)
		{
			SkyTimer t;

			t.Begin();

			if (part == 0)
				HDRenderSky(world);
			else if (part == 1)
				for (const SkyLayerArg& l : g_sky_layers)
					HDRenderSkyLayerProbe(world, page, l.colour, 0.0f,
						l.layer, g_sky_layer_psx ? g_sky_psx_windows : 0);
			else
				ProbeSkyCloud(world);

			ms.push_back(t.End());
		}

		std::sort(ms.begin(), ms.end());
		printf("sky gpu part=%s runs=%d median=%.4f min=%.4f max=%.4f ms\n",
			part == 0 ? "dome" : part == 1 ? "layers" : "cloud",
			(int)ms.size(), ms[ms.size() / 2], ms.front(), ms.back());
	}

	if (page && !g_sky_layer_hd)
		page->Release();
}

static int ProbeSkyCensus(const char* root, const char* stem, int rooms)
{
	trf::remaster::Trg trg;
	std::string error;

	if (!trf::remaster::read_trg(std::string(root) + "\\DATA\\" + stem +
		".TRG", trg, error))
	{
		printf("skycensus: no .TRG (%s)\n", error.c_str());
		return 2;
	}

	Tr5Level level;
	bool classic = false;

	if (!g_roombox.empty())
	{
		const std::string trc = g_roombox + "\\" + stem + ".TRC";

		classic = level.load(std::wstring(trc.begin(), trc.end()), error);
	}

	if (rooms <= 0 && classic)
		rooms = level.roomCount();

	if (rooms <= 0)
	{
		printf("skycensus: -rooms or -roombox needed\n");
		return 2;
	}

	FILE* dump = 0;

	if (!g_sky_dump.empty())
		fopen_s(&dump, g_sky_dump.c_str(), "w");

	printf("skycensus level=%s groups=%d rooms=%d sky=%d\n", stem,
		(int)trg.groups.size(), rooms,
		(int)trg.groups.size() > rooms ? (int)trg.groups.size() - rooms : 0);

	for (size_t g = (size_t)rooms; g < trg.groups.size(); g++)
	{
		const trf::remaster::TrgGroup& group = trg.groups[g];

		for (size_t p = 0; p < group.parts.size(); p++)
			for (int pass = 0; pass < 3; pass++)
			{
				const trf::remaster::TrgPart& part = group.parts[p];
				const unsigned int start = part.pass[pass].start;
				const unsigned int count = part.pass[pass].count;

				if (count < 3)
					continue;

				std::set<int> pages;
				std::map<int, std::pair<int, int> > rings;
				float y0 = 1e9f, y1 = -1e9f, r0 = 1e9f, r1 = -1e9f;
				float e0 = 1e9f, e1 = -1e9f, x0 = 1e9f, x1 = -1e9f;
				int u0 = 255, u1 = 0, v0 = 255, v1 = 0, l0 = 999, l1 = 0;
				unsigned int i0 = 0xFFFFFFFFu, i1 = 0;

				for (unsigned int i = 0; i < count; i++)
				{
					const unsigned int corner = trg.indices[start + i];
					const trf::remaster::TrgVertex& v = trg.vertices[corner];
					const float r = sqrtf((float)v.x * v.x +
						(float)v.z * v.z);
					const float e = atan2f(-(float)v.y, r) * 57.29578f;
					const int light = v.colour1[0] + v.colour1[1] +
						v.colour1[2] + v.colour2[0] + v.colour2[1] +
						v.colour2[2];

					if (v.layer >= 1 && v.layer <= trg.textures.size())
						pages.insert(trg.textures[v.layer - 1u]);

					y0 = (std::min)(y0, (float)v.y);
					y1 = (std::max)(y1, (float)v.y);
					r0 = (std::min)(r0, r);
					r1 = (std::max)(r1, r);
					e0 = (std::min)(e0, e);
					e1 = (std::max)(e1, e);
					x0 = (std::min)(x0, (float)v.x);
					x1 = (std::max)(x1, (float)v.x);
					u0 = (std::min)(u0, (int)v.u);
					u1 = (std::max)(u1, (int)v.u);
					v0 = (std::min)(v0, (int)v.v);
					v1 = (std::max)(v1, (int)v.v);
					l0 = (std::min)(l0, light);
					l1 = (std::max)(l1, light);
					i0 = (std::min)(i0, corner);
					i1 = (std::max)(i1, corner);

					std::map<int, std::pair<int, int> >::iterator ring =
						rings.find(v.y);

					if (ring == rings.end())
						rings[v.y] = std::make_pair((int)v.v, (int)v.v);
					else
					{
						ring->second.first = (std::min)(ring->second.first,
							(int)v.v);
						ring->second.second = (std::max)(ring->second.second,
							(int)v.v);
					}
				}

				printf("sky part group=%d part=%d pass=%d material=%u "
					"param=%.3f,%.3f,%.3f,%.3f tris=%u pages=", (int)g,
					(int)p, pass, part.material, part.param[0],
					part.param[1], part.param[2], part.param[3], count / 3);

				for (std::set<int>::const_iterator it = pages.begin();
					it != pages.end(); ++it)
					printf("%s%d", it == pages.begin() ? "" : ",", *it);

				printf(" y=%.0f..%.0f r=%.0f..%.0f elev=%.1f..%.1f "
					"u=%d..%d v=%d..%d light=%d..%d rings=%d "
					"vertices=%u..%u", y0, y1, r0, r1, e0, e1, u0, u1, v0, v1,
					l0, l1, (int)rings.size(), i0, i1);

				if (y0 == y1 && x1 > x0)
					printf(" sheet uv_per_1000=%.2f",
						(float)(u1 - u0) * 1000.0f / (x1 - x0));

				printf("\n");

				if (rings.size() > 1 && rings.size() <= 64)
					for (std::map<int, std::pair<int, int> >::const_iterator
						it = rings.begin(); it != rings.end(); ++it)
						printf("sky ring group=%d part=%d pass=%d y=%d "
							"v=%d..%d\n", (int)g, (int)p, pass, it->first,
							it->second.first, it->second.second);

				for (unsigned int i = 0; dump && i + 2 < count; i += 3)
				{
					fprintf(dump, "%d %d %d", (int)g, (int)p, pass);

					for (int c = 0; c < 3; c++)
					{
						const trf::remaster::TrgVertex& v =
							trg.vertices[trg.indices[start + i + c]];

						if (!c)
							fprintf(dump, " %d", v.layer >= 1 &&
								v.layer <= trg.textures.size()
								? (int)trg.textures[v.layer - 1u] : -1);

						fprintf(dump, " %d %d %d %d %d", v.x, v.y, v.z, v.u,
							v.v);
					}

					fprintf(dump, "\n");
				}
			}
	}

	if (dump)
		fclose(dump);

	/* Every page of the .TEX block 7000..7199 (the sky's panoramas and
	 * cloud sheets) the level's own page table names, sky part or not. */
	printf("sky pages level=%s table=", stem);

	for (size_t t = 0, n = 0; t < trg.textures.size(); t++)
		if (trg.textures[t] >= 7000 && trg.textures[t] < 7200)
			printf("%s%d", n++ ? "," : "", (int)trg.textures[t]);

	printf("\n");

	/* The classic HORIZON mesh: its vertices by height, and per height the
	 * page and the v pixels of the faces with a corner there. The mesh lies
	 * at the .TRC's mesh pointer of slot 454's first mesh: centre and radius
	 * (10 bytes), the vertex count and the vertices (6 bytes each), the
	 * normals or the shades, then the quads (12 bytes: four corners, the
	 * texinfo, the effects) and the triangles (10 bytes: three corners). */
	Tr5Level::MoveableInfo info;
	size_t at = 0, texAt = 0;
	int texCount = 0;

	if (!classic || !level.moveableInfo(454, info) || info.nmeshes < 1 ||
		!level.debugMeshAt(info.meshIndex, at) ||
		!level.debugTexinfoBlock(texAt, texCount))
	{
		printf("sky horizon: %s\n", classic ? "no HORIZON mesh in the level"
			: "no classic level (-roombox)");
		return 0;
	}

	const std::vector<uint8_t>& b = level.debugBytes();
	const auto word = [&](size_t o)
	{
		int16_t w = 0;

		if (o + 2 <= b.size())
			memcpy(&w, &b[o], 2);

		return (int)w;
	};
	const int verts = word(at + 10);
	std::vector<int> vy((size_t)(std::max)(verts, 0)), vr(vy.size());
	size_t o = at + 12;

	for (int v = 0; v < verts; v++, o += 6)
	{
		const int x = word(o), z = word(o + 4);

		vy[(size_t)v] = word(o + 2);
		vr[(size_t)v] = (int)sqrtf((float)(x * x + z * z));
	}

	const int normals = word(o);

	o += 2 + (size_t)(normals > 0 ? normals * 6 : -normals * 2);

	struct Ring
	{
		int r0, r1, v0, v1, faces;
		std::set<int> pages;
	};
	std::map<int, Ring> rings;
	int faces = 0;

	for (int kind = 4; kind >= 3; kind--)
	{
		const int n = word(o);

		o += 2;

		for (int f = 0; f < n; f++, o += (size_t)(kind * 2 + 4))
		{
			const int tex = word(o + (size_t)kind * 2) & 0x7FFF;
			const size_t t = texAt + (size_t)tex * 40;

			if (tex >= texCount || t + 22 > b.size())
				continue;

			faces++;

			for (int c = 0; c < kind; c++)
			{
				const int v = word(o + (size_t)c * 2);

				if (v < 0 || v >= verts)
					continue;

				const int py = (word(t + 8 + (size_t)c * 4) >> 8) & 0xFF;
				std::map<int, Ring>::iterator it = rings.find(vy[(size_t)v]);

				if (it == rings.end())
				{
					Ring r = { vr[(size_t)v], vr[(size_t)v], py, py, 0 };

					it = rings.insert(std::make_pair(vy[(size_t)v], r)).first;
				}

				Ring& r = it->second;

				r.r0 = (std::min)(r.r0, vr[(size_t)v]);
				r.r1 = (std::max)(r.r1, vr[(size_t)v]);
				r.v0 = (std::min)(r.v0, py);
				r.v1 = (std::max)(r.v1, py);
				r.faces++;
				r.pages.insert(word(t + 2) & 0x7FFF);
			}
		}
	}

	printf("sky horizon level=%s vertices=%d faces=%d rings=%d\n", stem,
		verts, faces, (int)rings.size());

	for (std::map<int, Ring>::const_iterator it = rings.begin();
		it != rings.end(); ++it)
	{
		printf("sky horizon ring y=%d r=%d..%d v=%d..%d corners=%d pages=",
			it->first, it->second.r0, it->second.r1, it->second.v0,
			it->second.v1, it->second.faces);

		for (std::set<int>::const_iterator p = it->second.pages.begin();
			p != it->second.pages.end(); ++p)
			printf("%s%d", p == it->second.pages.begin() ? "" : ",", *p);

		printf("\n");
	}

	return 0;
}

static int ProbeMain(int argc, char** argv)
{
	/* -loadtime, -reload and -reloadas are taken out of the list here, ahead
	 * of the switch chain below, which stands at the compiler's nesting
	 * depth. */
	std::vector<char*> kept;

	for (int i = 0; i < argc; i++)
		if (!strcmp(argv[i], "-loadtime"))
			g_loadtime = true;
		else if (!strcmp(argv[i], "-reload"))
			g_reload = true;
		else if (!strcmp(argv[i], "-ahead") ||
			!strcmp(argv[i], "-aheadverify"))
		{
			g_ahead_pics = true;
			g_ahead_verify = g_ahead_verify || !strcmp(argv[i], "-aheadverify");
		}
		else if (!strcmp(argv[i], "-reloadas") && i + 1 < argc)
		{
			g_reload = true;
			g_reload_as = argv[++i];
		}
		else
			kept.push_back(argv[i]);

	argc = (int)kept.size();
	argv = &kept[0];

	if (argc < 4)
	{
		printf("usage: RoomProbe <5-folder> <LEVEL> <room> [x y z yaw pitch] [options]\n");
		printf("       -size <w> <h>  -fov <deg>  -all  -managed  -threads <n>\n");
		printf("       -rooms <n>  -out <prefix>  -groups  -nodraw  -matrixcheck\n");
		printf("       -light <x y z r g b falloff>   -bulb <x y z rad den [r g b]>\n");
		printf("       -fog <start end r g b>  -underwater  -shimmer  -wibble <n>\n");
		printf("       -effect <0..21>  -paritycheck\n");
		printf("       -sky  -skyonly  -skycensus  -skydump <path>   the HD "
			"sky drawn, alone, or its parts counted\n");
		printf("       -skylayer <1|2> <r> <g> <b> <vel>  "
			"-skylayerpage sd|hd|psx   a classic layer over the HD sky\n");
		printf("       -skycloud <floor> <thickness> <cell> <coverage> "
			"<density> <vel>  -skygpu <n>   the cloud measurement\n");
		printf("       -time <seconds>  -water  -nomotion  -animtex\n");
		printf("       -hdwater 0|1  -waterfile <path>   the water sheets "
			"(needs -roombox)\n");
		printf("       -nocopy  -nodepth  -waterdepthview 1|2|3|4|5  "
			"-waterbend <s>   the sheets without the frame's copy or depth "
			"texture, the depth as grey or the patches in green, the "
			"refraction scaled\n");
		printf("       -ripple x,z,age[,size]   a ring on the sheets at world "
			"x, z, age seconds old (size 120), repeatable\n");
		printf("       -lara x,z   Lara's place in the water: no waterline "
			"round it\n");
		printf("       -creature x,z   a stand-in for a swimming creature at "
			"world x, z, drawn with the creatures' mark, repeatable; "
			"-nomark draws it without, -markview shows the mark in red\n");
		printf("       -life <seed>   the water's life on the sheets "
			"(with -hdwater 1): the floats, the fish and the drips' rings "
			"the game builds, rolled from <seed>, stepped to -time and "
			"drawn as the game draws them; LIFELOG lines\n");
		printf("       -lifelara x,z[,t]   Lara's place for -life from clock "
			"t: the school flees her, the drips near her ring\n");
		printf("       -lifetank 0|1   a fish tank shot (0: its fish "
			"gone) or whole (1, the default)\n");
		printf("       -lifesplash x,z,t   a splash on the water at "
			"world x, z for two seconds from clock t: the school of that "
			"water darts off, repeatable\n");
		printf("       -lifebench   -life's draws timed on a second pass "
			"(every model drawn twice: numbers, not a picture)\n");
		printf("       -lifeall   -life on every body, whatever the "
			"level's life row lets in\n");
		printf("       -lifeseries <s>   with -life: a LIFESERIES line a "
			"school every s seconds (each fish's place, depth and distance "
			"to the deep water's edge) and the bodies' LIFESQUARES\n");
		printf("       -wader x,z   a stand-in for a wading Lara at world x, "
			"z, standing on the floor under the sheet, with Lara's place "
			"there, repeatable\n");
		printf("       -twophase   the solid and the blend phase in two "
			"brackets, as the game draws them; with -creature the mark's "
			"WATERLOG line is printed\n");
		printf("       -foot x1,z1,x2,z2,r   a waterfall's foot on the "
			"sheets along that line, reach r, repeatable; -swirl x,z,r one "
			"of length 0\n");
		printf("       -bookpage <n>   every material 15 book on page n of "
			"its run, whatever -time says\n");
		printf("       -with <group> <dx dy dz>   -oldorder   -black   -noshore\n");
		printf("       -glow   the additive pass alone over black, with and "
			"without the scene's depth\n");
		printf("       -stackedseas  -fastswing  -addmat <material>\n");
		printf("       -glowblend add|screen  -aniso 0|1  -glowps 0|1\n");
		printf("       -scissor <l,t,r,b>\n");
		printf("       -devicecycle   the run on three devices, Alt+Enter's "
			"way; -crash <folder>   the crash seam's own test\n");
		printf("       -cycleclassic <pages> [-nomove]   the classic pages "
			"across three devices\n");
		printf("       -windowcycle [-leak]   the game window and its swap "
			"chain across two toggles\n");
		printf("       -roombox <folder holding the classic .TRC>\n");
		printf("       -classicroom <room>   that room's classic faces, flat, "
			"beside the art (needs -roombox)\n");
		printf("       -classiconly  -coincide <units>  -coincidemark   the "
			"classic faces alone; the\n");
		printf("                     pixels where they land within <units> of "
			"the art, and the art's of them,\n");
		printf("                     counted (and painted yellow and red)\n");
		printf("       -stand   the frame's rule for stand art (hd_cover.h): "
			"-classicroom rooms listed,\n");
		printf("                the seed and -with groups the listed art "
			"(needs -roombox)\n");
		printf("       -standreceive   -stand, and the shadow receive of each range "
			"drawn in a holder's\n");
		printf("                window counted at its own depth and at the other "
			"push\n");
		printf("       -skipgroup <g>   group g left out of the picture and "
			"not listed\n");
		printf("       -statictable   every static's answer, old and new, the "
			"shares and the shatterable\n");
		printf("                      ones, then no picture (needs -roombox)\n");
		printf("       -smash <room>:<mesh>   that static shot: its pieces "
			"hidden as the engine pairs them (rooms after -flip)\n");
		printf("       -smashcensus   the destructible pieces, the shatterable "
			"statics and their pairs\n");
		printf("       -smashpiece <n>   destructible piece n hidden, in any "
			"group; repeatable\n");
		printf("       -statics new|old   the classic statics a frame of this "
			"picture draws, flat, and with new the statics' pieces\n");
		printf("       -piece <room>:<mesh>   with -statics new, only that "
			"static, as its piece or classic; repeatable\n");
		printf("       -listroom <room>   that room listed for the statics, "
			"nothing of it drawn; repeatable\n");
		printf("       -static <room>:<mesh>   that static, whatever a rule says;"
			"  -staticsonly   alone\n");
		printf("       -hdm                          apply the .hdm sidecar "
			"beside it (needs -roombox)\n");
		printf("       -window all|open|intruding   (needs -roombox)\n");
		printf("       -cull none|opaque   the opaque ranges two-sided or "
			"one-sided (default)\n");
		printf("       -roomwin <g>:<l>,<t>,<r>,<b>[;...]   a group's walk window,"
			" drawn as\n");
		printf("                                      DrawRoomWindowed does "
			"(needs -roombox)\n");
		printf("       -decide whole|scissor|frame|face   the pieces of intruding"
			" art (default frame)\n");
		printf("       -unlinked 0|1|2   art in an unlinked room's air inside"
			" its own window (default 1; 2 keeps the smaller box's answer in"
			" an empty window)\n");
		printf("       -air 0|1|2      tomb5.hd_air: the rooms' air handed to"
			" the build (default 1; 2 the hulls too)\n");
		printf("       -clipgroup <group> <l,t,r,b>   one group's own window\n");
		printf("       -tround <group> <l,t,r,b>      one group in two halves\n");
		printf("       -walk <seed room> <x y z>      the classic portal walk,\n");
		printf("                                      offline (needs -roombox)\n");
		printf("       -walkyaw <from> <to> <step>   default 0 359 15\n");
		printf("       -walkpitch <deg>  -walkpitchfan <from> <to> <step>\n");
		printf("       -walkwant <room>  -walkeach  -walkcentre  -walkfinal\n");
		printf("       -walktarget <x y z>   the engine's camera from eye and "
			"target\n");
		printf("       -walkshut <item,...|all>   those closed doors shut\n");
		printf("       -walkdump <log> [frame]   a log's whole-list dumps "
			"walked again\n");
		printf("       -reach   the two-rectangle portal walk\n");
		printf("       -walkboxes   every room's origin, for -with\n");
		printf("       -reachsurvey [step]   every room of the level as the\n");
		printf("                             eye, both rules, one line\n");
		printf("       -marblecensus  -marblequiet  -marblepages a,b,...   the "
			"polished floors: marblesq/marbleroom/marblepage/marblefloor/"
			"marblelevel lines (with -roombox)\n");
		printf("       -marble 0|1  -marbleplanes n  -marblerows 1|2   the "
			"engine's marble floors' mirror, timed (marble: line)\n");
		printf("       -budget <MiB>  -format rgba|dxt5  -psnr\n");
		printf("       -caps maxtex <n>  -caps noidx32  -seq <A,B,C>  -vramhog\n");
		printf("       -loadtime   the HD build step by step as LOADLOG "
			"lines, no picture (with -roombox)\n");
		printf("       -spring x,z,s   one more spring at world x, z of "
			"strength s (0..1), with -hdwater 1, repeatable; -nospring "
			"none; -springparts 1|2 the columns or the bubbles alone\n");
		printf("       -springs   the level's springs as the engine builds "
			"them, their SPRINGLOG lines and the rule's SPRINGRULE line per "
			"body (-hdwater 1; with -nodraw no picture); -springkey <n> the "
			"key hd_springs (1)\n");
		printf("       -reload   a savegame loaded inside the level: the "
			"build kept by the engine's key, the sheets set again, then the "
			"pictures; -reloadas <LEVEL> another level's key: the run made "
			"again for that level in this process, its fingerprint printed, "
			"no picture (with -ahead: its .TRG and first pages read ahead "
			"beside this level's build, the commit printed)\n");
		printf("       -ahead   the engine's .TRG read and first pages "
			"decoded ahead of the build (with -roombox), then the pictures; "
			"-aheadverify also decodes every page taken again and counts "
			"those that differ\n");
		printf("       -springcrack 0|1   the springs' cracks, as the key "
			"hd_springcrack (1); -springs lists each crack and its outline "
			"(SPRINGCRACK lines); -springcrackmap the outline over its "
			"body's squares as text (-hdwater 1)\n");
		return 1;
	}

	const char* root = argv[1];
	const char* stem = argv[2];
	const int room = atoi(argv[3]);

	double cx = 0, cy = 0, cz = 0, yaw = 0, pitch = 0;
	bool haveCamera = false;
	long width = 1280, height = 720;
	double fov = 80.0;
	bool allPasses = false, managed = false, groupsOnly = false, noDraw = false;
	bool matrixCheck = false;
	bool parityCheck = false;
	bool animTex = false;
	// Draw the .TRG's sky group(s) behind the room, and only them.
	bool sky = false, skyOnly = false;
	bool wibbleGiven = false;
	// -glowblend, -aniso and -glowps, handed to HDRenderGlowOverride together.
	int glowSum = 0, glowNoAniso = 0, glowNoShader = 0;

	// The portal walk offline. A seed of -1 means the switch was
	// not given and nothing below changes.
	int walkSeed = -1, walkWant = -1;
	long walkEye[3] = { 0, 0, 0 };
	double walkYaw[3] = { 0.0, 359.0, 15.0 };
	double walkPitch[3] = { 0.0, 0.0, 1.0 };
	bool walkEach = false;
	/* -walktarget: the engine's camera from the eye and a world target;
	 * -walkfinal: every listed room's final window; -walkdump: a log's
	 * whole-list dumps walked again, all of them or the one at a frame. */
	long walkTarget[3] = { 0, 0, 0 };
	bool walkTargetGiven = false, walkFinal = false;
	const char* walkDump = 0;
	long walkDumpFrame = -1;
	// The reach, the eye at the seed room's centre, and the level survey.
	bool walkReach = false, walkCentre = false, reachSurvey = false;
	bool walkBoxes = false;
	int reachStep = 1;
	int threads = 8, rooms = 0;

	// The texture budget and the small card.
	double budgetMiB = 0;
	int pageFormat = HD_PAGE_FORMAT_RGBA;
	bool psnr = false;
	int capMaxTex = 0, capNoIndex32 = 0;
	const char* sequence = 0;
	bool vramHog = false;

	HDLightBuildWaterTable(g_water_table);
	const char* out = "roomprobe";

	int at = 4;

	// The five camera numbers, when they are there, come before the options.
	if (argc >= 9 && argv[4][0] != '-')
	{
		cx = atof(argv[4]);
		cy = atof(argv[5]);
		cz = atof(argv[6]);
		yaw = atof(argv[7]);
		pitch = atof(argv[8]);
		haveCamera = true;
		at = 9;
	}

	for (; at < argc; at++)
	{
		if (SkyArg(argc, argv, &at))
			continue;

		// The springs' switches: the chain below is at the compiler's limit
		// of nested blocks (C1061).
		if (SpringArg(argc, argv, &at))
			continue;

		if (VisArg(argc, argv, &at))
			continue;

		if (MarbleArg(argc, argv, &at))
			continue;

		if (SmashArg(argc, argv, &at))
			continue;

		if (UnderArg(argc, argv, &at))
			continue;

		if (!strcmp(argv[at], "-size") && at + 2 < argc)
		{
			width = atol(argv[at + 1]);
			height = atol(argv[at + 2]);
			at += 2;
		}
		else if (!strcmp(argv[at], "-fov") && at + 1 < argc)
			fov = atof(argv[++at]);
		else if (!strcmp(argv[at], "-threads") && at + 1 < argc)
			threads = HDPoolThreads(atoi(argv[++at]));
		else if (!strcmp(argv[at], "-rooms") && at + 1 < argc)
			rooms = atoi(argv[++at]);
		else if (!strcmp(argv[at], "-out") && at + 1 < argc)
			out = argv[++at];
		else if (!strcmp(argv[at], "-all"))
			allPasses = true;
		else if (!strcmp(argv[at], "-managed"))
			managed = true;
		else if (!strcmp(argv[at], "-groups"))
			groupsOnly = true;
		else if (!strcmp(argv[at], "-nodraw"))
			noDraw = true;
		else if (!strcmp(argv[at], "-matrixcheck"))
			matrixCheck = true;
		else if (!strcmp(argv[at], "-paritycheck"))
			parityCheck = true;
		else if (!strcmp(argv[at], "-underwater"))
			g_underwater = true;
		else if (!strcmp(argv[at], "-time") && at + 1 < argc)
		{
			g_time = atof(argv[at + 1]);
			at++;
		}
		else if (!strcmp(argv[at], "-water"))
			g_water = true;
		else if (!strcmp(argv[at], "-hdwater") && at + 1 < argc)
			g_hdwater = atoi(argv[++at]) ? 1 : 0;
		else if (!strcmp(argv[at], "-waterfile") && at + 1 < argc)
			g_water_file = argv[++at];
		else if (!strcmp(argv[at], "-nocopy"))
			g_water_copy = false;
		else if (!strcmp(argv[at], "-nodepth"))
			g_water_depth = false;
		else if (!strcmp(argv[at], "-waterdepthview") && at + 1 < argc)
			g_water_depth_view = atoi(argv[++at]);
		else if (!strcmp(argv[at], "-waterbend") && at + 1 < argc)
			g_water_bend = (float)atof(argv[++at]);
		else if (!strcmp(argv[at], "-ripple") && at + 1 < argc)
		{
			ProbeRipple r = { 0.0f, 0.0f, 0.0f, 120.0f };

			if (sscanf_s(argv[++at], "%f,%f,%f,%f", &r.x, &r.z, &r.age,
				&r.size) >= 3)
				g_ripples.push_back(r);
			else
				printf("RoomProbe: -ripple wants x,z,age[,size] - '%s' "
					"ignored\n", argv[at]);
		}
		else if (!strcmp(argv[at], "-splash") && at + 4 < argc)
		{
			for (int k = 0; k < 4; k++)
				g_splash_at[k] = (float)atof(argv[at + 1 + k]);

			g_splash_set = true;
			at += 4;
		}
		else if (!strcmp(argv[at], "-splashkind") && at + 1 < argc)
			g_splash_kind = atoi(argv[++at]);
		else if (!strcmp(argv[at], "-splashfall") && at + 1 < argc)
			g_splash_fall = atoi(argv[++at]);
		else if ((!strcmp(argv[at], "-tank") && at + 2 < argc) ||
			(!strcmp(argv[at], "-tankleave") && at + 1 < argc) ||
			(!strcmp(argv[at], "-tankwet") && at + 1 < argc) ||
			(!strcmp(argv[at], "-tankmarch") && at + 1 < argc) ||
			!strcmp(argv[at], "-tankwetmap") ||
			!strcmp(argv[at], "-tankart") ||
			!strcmp(argv[at], "-tankcensus") ||
			(!strcmp(argv[at], "-wetstand") && at + 1 < argc) ||
			(!strcmp(argv[at], "-wetlara") && at + 1 < argc) ||
			!strcmp(argv[at], "-wetagain") ||
			(!strcmp(argv[at], "-wetblur") && at + 1 < argc) ||
			!strcmp(argv[at], "-wetnomirror") ||
			(!strcmp(argv[at], "-wetstep") && at + 1 < argc) ||
			(!strcmp(argv[at], "-wetcut") && at + 1 < argc) ||
			!strcmp(argv[at], "-wetnomodels") ||
			!strcmp(argv[at], "-tankslopes") ||
			!strcmp(argv[at], "-tankwetall"))
		{
			// One branch for all: the chain is at the compiler's depth.
			if (!strcmp(argv[at], "-tankwetmap"))
				g_tank_wet_map = true;
			else if (!strcmp(argv[at], "-tankart"))
				g_tank_art = true;
			else if (!strcmp(argv[at], "-tankcensus"))
				g_tank_census = true;
			else if (!strcmp(argv[at], "-wetnomodels"))
				g_wet_no_models = true;
			else if (!strcmp(argv[at], "-wetagain"))
				g_wet_again = g_wet_again_split = true;
			else if (!strcmp(argv[at], "-wetblur"))
			{
				float k = -1.0f, whole = -1.0f;

				sscanf_s(argv[++at], "%f,%f", &k, &whole);
				HDRenderWaterWetBlur(k, whole);
			}
			else if (!strcmp(argv[at], "-wetnomirror"))
				g_wet_no_mirror = true;
			else if (!strcmp(argv[at], "-wetstep"))
				g_wet_step = sscanf_s(argv[++at], "%d,%d,%d,%d",
					&g_wet_step_at[0], &g_wet_step_at[1], &g_wet_step_at[2],
					&g_wet_step_at[3]) == 4;
			else if (!strcmp(argv[at], "-wetcut"))
				g_wet_cut = sscanf_s(argv[++at], "%d,%d,%d,%d",
					&g_wet_cut_key[0], &g_wet_cut_key[1], &g_wet_cut_key[2],
					&g_wet_cut_key[3]) == 4;
			else if (!strcmp(argv[at], "-wetlara"))
				g_wet_lara = sscanf_s(argv[++at], "%f,%f,%f,%f,%d",
					&g_wet_lara_at[0], &g_wet_lara_at[1], &g_wet_lara_at[2],
					&g_wet_lara_at[3], &g_wet_lara_pose) >= 3;
			else if (!strcmp(argv[at], "-wetstand"))
			{
				float s[3] = { 0.0f, 0.0f, 768.0f };
				int a = g_wet_stand_alpha;

				if (sscanf_s(argv[++at], "%f,%f,%f,%d", &s[0], &s[1], &s[2],
					&a) >= 2 && a >= 0 && a <= 255)
				{
					g_wet_stands.insert(g_wet_stands.end(), s, s + 3);
					g_wet_stand_alpha = a;
				}
				else
					printf("RoomProbe: -wetstand wants x,z[,h[,a]] - '%s' "
						"ignored\n", argv[at]);
			}
			else if (!strcmp(argv[at], "-tankslopes"))
				g_tank_slopes = true;
			else if (!strcmp(argv[at], "-tankwetall"))
				HDRenderWaterWetLimited(0);
			else if (!strcmp(argv[at], "-tankmarch"))
			{
				// map[,fit,growth,first]: a trial of the march.
				float t[3] = { 0.0f, 0.0f, 0.0f };
				const int map = atoi(argv[++at]);

				sscanf_s(argv[at], "%*d,%f,%f,%f", &t[0], &t[1], &t[2]);
				HDRenderWaterWetMarch(map, t);
			}
			else if (argv[at][5] == 'w')
				g_tank_wet = (float)atof(argv[++at]);
			else if (argv[at][5])
			{
				at++;
				g_tank_leave |= !strcmp(argv[at], "spray") ? 1 :
					!strcmp(argv[at], "water") ? 2 : 0;
			}
			else
			{
				g_tank_item = atoi(argv[at + 1]);
				g_tank_time = (float)atof(argv[at + 2]);
				at += 2;
			}
		}
		else if (!strcmp(argv[at], "-lara") && at + 1 < argc)
		{
			g_lara_set = sscanf_s(argv[++at], "%f,%f", &g_lara_x,
				&g_lara_z) == 2;

			if (!g_lara_set)
				printf("RoomProbe: -lara wants x,z - '%s' ignored\n",
					argv[at]);
		}
		else if (!strcmp(argv[at], "-swirl") && at + 1 < argc)
		{
			float s[3] = { 0.0f, 0.0f, 0.0f };

			if (sscanf_s(argv[++at], "%f,%f,%f", &s[0], &s[1], &s[2]) == 3 &&
				s[2] >= 64.0f && s[2] <= 2048.0f)
			{
				const float f[5] = { s[0], s[1], s[0], s[1], s[2] };

				g_feet.insert(g_feet.end(), f, f + 5);
			}
			else
				printf("RoomProbe: -swirl wants x,z,r (r 64 to 2048) - '%s' "
					"ignored\n", argv[at]);
		}
		else if (!strcmp(argv[at], "-foot") && at + 1 < argc)
		{
			float f[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

			if (sscanf_s(argv[++at], "%f,%f,%f,%f,%f", &f[0], &f[1], &f[2],
				&f[3], &f[4]) == 5 && f[4] >= 64.0f && f[4] <= 2048.0f)
				g_feet.insert(g_feet.end(), f, f + 5);
			else
				printf("RoomProbe: -foot wants x1,z1,x2,z2,r (r 64 to 2048) - "
					"'%s' ignored\n", argv[at]);
		}
		else if (!strcmp(argv[at], "-creature") && at + 1 < argc)
		{
			float c[2] = { 0.0f, 0.0f };

			if (sscanf_s(argv[++at], "%f,%f", &c[0], &c[1]) == 2)
				g_creatures.insert(g_creatures.end(), c, c + 2);
			else
				printf("RoomProbe: -creature wants x,z - '%s' ignored\n",
					argv[at]);
		}
		else if (!strcmp(argv[at], "-life") && at + 1 < argc)
		{
			g_life = true;
			g_life_seed = (unsigned)strtoul(argv[++at], 0, 10);
			/* And the game's two brackets (-twophase): the models' draws
			 * leave their constants in registers the sheets read, and the
			 * game's second bracket uploads the frame's again before the
			 * sheets draw (hd_level.cpp, HDFrameDrawGlow). */
			g_two_phase = true;
		}
		else if (!strcmp(argv[at], "-lifebench"))
			g_life_bench = true;
		else if (!strcmp(argv[at], "-lifeall"))
			g_life_all = true;
		/* -lifesplash, -lifetank and -liferings share this branch: one more
		 * `else if` passes the compiler's limit of nested blocks (C1061). */
		else if ((!strcmp(argv[at], "-lifelara") ||
			!strcmp(argv[at], "-lifesplash") ||
			!strcmp(argv[at], "-lifetank") ||
			!strcmp(argv[at], "-lifeseries") ||
			!strcmp(argv[at], "-liferings")) && at + 1 < argc)
		{
			float c[3] = { 0.0f, 0.0f, 0.0f };

			if (!strcmp(argv[at], "-lifeseries"))
				g_life_series = (float)atof(argv[++at]);
			else if (!strcmp(argv[at], "-liferings"))
			{
				at++;
				g_life_rings = !strcmp(argv[at], "game") ? 2 :
					atoi(argv[at]) ? 1 : 0;
			}
			else if (!strcmp(argv[at], "-lifetank"))
			{
				g_life_tank_game = !strcmp(argv[++at], "game");
				g_life_tank_whole = g_life_tank_game || atoi(argv[at]) != 0;
			}
			else if (!strcmp(argv[at], "-lifesplash"))
			{
				if (sscanf_s(argv[++at], "%f,%f,%f", &c[0], &c[1], &c[2]) == 3)
					g_life_splash.insert(g_life_splash.end(), c, c + 3);
				else
					printf("RoomProbe: -lifesplash wants x,z,t - '%s' "
						"ignored\n", argv[at]);
			}
			else if (sscanf_s(argv[++at], "%f,%f,%f", &c[0], &c[1], &c[2]) >= 2)
			{
				g_life_lara = true;
				g_life_lara_x = c[0];
				g_life_lara_z = c[1];
				g_life_lara_t = c[2];
			}
			else
				printf("RoomProbe: -lifelara wants x,z[,t] - '%s' "
					"ignored\n", argv[at]);
		}
		/* -swimmer shares this branch, for the compiler's limit of nested
		 * blocks (C1061) - see -lifesplash. */
		else if ((!strcmp(argv[at], "-wader") ||
			!strcmp(argv[at], "-swimmer")) && at + 1 < argc)
		{
			float c[3] = { 0.0f, 0.0f, 0.0f };

			if (!strcmp(argv[at], "-swimmer"))
			{
				if (sscanf_s(argv[++at], "%f,%f,%f", &c[0], &c[1], &c[2]) == 3)
					g_swimmers.insert(g_swimmers.end(), c, c + 3);
				else
					printf("RoomProbe: -swimmer wants x,z,below - '%s' "
						"ignored\n", argv[at]);
			}
			else if (sscanf_s(argv[++at], "%f,%f", &c[0], &c[1]) == 2)
			{
				g_waders.insert(g_waders.end(), c, c + 2);
				g_lara_set = true;
				g_lara_x = c[0];
				g_lara_z = c[1];
			}
			else
				printf("RoomProbe: -wader wants x,z - '%s' ignored\n",
					argv[at]);
		}
		else if (!strcmp(argv[at], "-nomark"))
			g_creature_mark = false;
		else if (!strcmp(argv[at], "-latemark"))
			g_creature_late = true;
		else if (!strcmp(argv[at], "-rippleat") && at + 1 < argc)
		{
			int r[3] = { 0, 0, 0 };

			if (sscanf_s(argv[++at], "%d,%d,%d", &r[0], &r[1], &r[2]) == 3)
				g_ripple_at.insert(g_ripple_at.end(), r, r + 3);
			else
				printf("RoomProbe: -rippleat wants x,y,z - '%s' ignored\n",
					argv[at]);
		}
		else if (!strcmp(argv[at], "-twophase"))
			g_two_phase = true;
		else if (!strcmp(argv[at], "-markview"))
			g_mark_view = true;
		else if ((!strcmp(argv[at], "-plane") ||
			!strcmp(argv[at], "-roomplane")) && at + 5 < argc)
		{
			const short y = (short)atoi(argv[at + 1]);
			const short x0 = (short)atoi(argv[at + 2]);
			const short z0 = (short)atoi(argv[at + 3]);
			const short x1 = (short)atoi(argv[at + 4]);
			const short z1 = (short)atoi(argv[at + 5]);
			/* -roomplane is the other shimmer bit: the ROOM_REFLECT +
			 * ROOM_REFLECT_CEILING answer, which is what the Rome levels'
			 * water carries. */
			const unsigned char bits =
				argv[at][1] == 'r' ? (unsigned char)4 : (unsigned char)2;

			for (int c = 0; c < 4; c++)
			{
				g_plane_marks[c].room = (short)room;
				g_plane_marks[c].y = y;
				g_plane_marks[c].x = (c & 1) ? x1 : x0;
				g_plane_marks[c].z = (c & 2) ? z1 : z0;
				g_plane_marks[c].bits = bits;
			}

			g_plane = true;
			at += 5;
		}
		else if (!strcmp(argv[at], "-scissor") && at + 1 < argc)
		{
			/* "l,t,r,b" in backbuffer pixels, left and top the first pixel
			 * drawn and right and bottom the first one that is not - the
			 * same half-open rectangle HDRenderScissor takes and the same
			 * one the engine hands it. */
			if (sscanf(argv[at + 1], "%ld,%ld,%ld,%ld", &g_scissor_box[0],
				&g_scissor_box[1], &g_scissor_box[2], &g_scissor_box[3]) == 4)
				g_scissor = true;
			else
				printf("RoomProbe: -scissor wants l,t,r,b with commas and no "
					"spaces - \"%s\" is not that\n", argv[at + 1]);

			at += 1;
		}
		else if (!strcmp(argv[at], "-nomotion"))
			g_motion = false;
		else if (!strcmp(argv[at], "-bookpage") && at + 1 < argc)
		{
			HDRenderBookPageOverride(atoi(argv[at + 1]));
			at++;
		}
		else if (!strcmp(argv[at], "-black"))
			g_black = true;
		else if (!strcmp(argv[at], "-glow"))
			g_glow_alone = true;
		else if (!strcmp(argv[at], "-stackedseas"))
			HDRenderStackOverride(1);
		else if (!strcmp(argv[at], "-fastswing"))
			HDRenderSwingOverride(1);
		else if (!strcmp(argv[at], "-addmat") && at + 1 < argc)
			HDRenderAdditiveOnly(atoi(argv[++at]));
		else if (!strcmp(argv[at], "-glowblend") && at + 1 < argc)
		{
			glowSum = !strcmp(argv[++at], "add") ? 1 : 0;
			HDRenderGlowOverride(glowSum, glowNoAniso, glowNoShader);
		}
		else if (!strcmp(argv[at], "-aniso") && at + 1 < argc)
		{
			glowNoAniso = atoi(argv[++at]) ? 0 : 1;
			HDRenderGlowOverride(glowSum, glowNoAniso, glowNoShader);
		}
		else if (!strcmp(argv[at], "-glowps") && at + 1 < argc)
		{
			glowNoShader = atoi(argv[++at]) ? 0 : 1;
			HDRenderGlowOverride(glowSum, glowNoAniso, glowNoShader);
		}
		else if (!strcmp(argv[at], "-noshore"))
			HDRenderShoreOverride(1);
		else if (!strcmp(argv[at], "-noblend"))
			HDRenderBlendOverride(1);
		else if (!strcmp(argv[at], "-noclear"))
			HDRenderClearOverride(1);
		else if (!strcmp(argv[at], "-noupright"))
			HDRenderUprightOverride(1);
		else if (!strcmp(argv[at], "-noreflect"))
			g_reflect = false;
		else if (!strcmp(argv[at], "-glossmax"))
			g_gloss_max = true;
		else if (!strcmp(argv[at], "-glossfield"))
		{
			g_gloss_max = true;
			g_gloss_field = 1;

			if (at + 1 < argc && !strcmp(argv[at + 1], "direction"))
			{
				g_gloss_field = 2;
				at++;
			}
			else if (at + 1 < argc && !strcmp(argv[at + 1], "colour"))
				at++;
		}
		else if (!strcmp(argv[at], "-glossfile") && at + 1 < argc)
		{
			g_gloss_file = argv[at + 1];
			at++;
		}
		else if (!strcmp(argv[at], "-fieldsight"))
			g_field_sight = true;
		else if (!strcmp(argv[at], "-shatter"))
			g_shatter = true;
		else if (!strcmp(argv[at], "-with") && at + 4 < argc)
		{
			if (g_with_count >= kWithMax)
			{
				printf("-with: more than %d groups - the picture would leave "
					"rooms out, so nothing is drawn\n", kWithMax);
				return 1;
			}

			g_with_group[g_with_count] = atoi(argv[at + 1]);

			for (int c = 0; c < 3; c++)
				g_with_offset[g_with_count][c] = (float)atof(argv[at + 2 + c]);

			g_with_count++;
			at += 4;
		}
		else if (!strcmp(argv[at], "-oldorder"))
			g_old_order = true;
		else if (!strcmp(argv[at], "-watersurvey") && at + 1 < argc)
		{
			g_survey = argv[at + 1];
			at += 1;
		}
		else if (!strcmp(argv[at], "-roombox") && at + 1 < argc)
		{
			g_roombox = argv[at + 1];
			at += 1;
		}
		else if (!strcmp(argv[at], "-watermarks"))
			g_watermarks = true;
		else if (!strcmp(argv[at], "-hdm"))
			g_hdm = true;
		else if ((!strcmp(argv[at], "-classicroom") ||
			!strcmp(argv[at], "-classicmark")) && at + 1 < argc)
		{
			// -classicmark is a -classicroom drawn red.
			if (!strcmp(argv[at], "-classicmark"))
				g_classic_mark.insert(atoi(argv[at + 1]));

			g_classic_rooms.push_back(atoi(argv[++at]));
		}
		else if (!strcmp(argv[at], "-classiconly") ||
			!strcmp(argv[at], "-classicclip"))
			(argv[at][8] == 'o' ? g_classic_only : g_classic_clip) = true;
		else if (!strcmp(argv[at], "-classicplane") && at + 1 < argc)
		{
			g_classic_plane = atol(argv[++at]);
			g_classic_plane_set = true;
		}
		else if (!strcmp(argv[at], "-classicwet"))
			g_classic_wet = true;
		else if (!strcmp(argv[at], "-coincide") && at + 1 < argc)
			g_coincide = atof(argv[++at]);
		else if (!strcmp(argv[at], "-coincidemark"))
			g_coincide_mark = true;
		else if (!strcmp(argv[at], "-stand"))
			g_stand_rule = true;
		else if (!strcmp(argv[at], "-standreceive"))
			g_stand_rule = g_stand_receive = true;
		else if (!strcmp(argv[at], "-statictable"))
			probestatics::g_table = true;
		else if (!strcmp(argv[at], "-statics") && at + 1 < argc)
		{
			++at;
			probestatics::g_rule = !strcmp(argv[at], "old") ? 2 :
				!strcmp(argv[at], "new") ? 1 : 0;
		}
		else if (!strcmp(argv[at], "-static") && at + 1 < argc)
		{
			int r = -1, m = -1;

			if (sscanf(argv[++at], "%d:%d", &r, &m) == 2)
				probestatics::g_forced.push_back(std::make_pair(r, m));
		}
		else if (!strcmp(argv[at], "-piece") && at + 1 < argc)
		{
			int r = -1, m = -1;

			if (sscanf(argv[++at], "%d:%d", &r, &m) == 2)
				probestatics::g_piece_only.push_back(std::make_pair(r, m));
		}
		else if (!strcmp(argv[at], "-listroom") && at + 1 < argc)
			probestatics::g_list_rooms.push_back(atoi(argv[++at]));
		else if (!strcmp(argv[at], "-staticsonly"))
			probestatics::g_only = true;
		else if (!strcmp(argv[at], "-skipgroup") && at + 1 < argc)
			g_skip_groups.insert(atoi(argv[++at]));
		else if (!strcmp(argv[at], "-walk") && at + 4 < argc)
		{
			walkSeed = atoi(argv[at + 1]);
			walkEye[0] = atol(argv[at + 2]);
			walkEye[1] = atol(argv[at + 3]);
			walkEye[2] = atol(argv[at + 4]);
			at += 4;
		}
		else if (!strcmp(argv[at], "-walkyaw") && at + 3 < argc)
		{
			walkYaw[0] = atof(argv[at + 1]);
			walkYaw[1] = atof(argv[at + 2]);
			walkYaw[2] = atof(argv[at + 3]);
			at += 3;
		}
		else if (!strcmp(argv[at], "-walkpitch") && at + 1 < argc)
		{
			walkPitch[0] = walkPitch[1] = atof(argv[++at]);
			walkPitch[2] = 1.0;
		}
		else if (!strcmp(argv[at], "-walkpitchfan") && at + 3 < argc)
		{
			walkPitch[0] = atof(argv[at + 1]);
			walkPitch[1] = atof(argv[at + 2]);
			walkPitch[2] = atof(argv[at + 3]);
			at += 3;
		}
		else if (!strcmp(argv[at], "-walkwant") && at + 1 < argc)
			walkWant = atoi(argv[++at]);
		else if (!strcmp(argv[at], "-walktarget") && at + 3 < argc)
		{
			walkTarget[0] = atol(argv[at + 1]);
			walkTarget[1] = atol(argv[at + 2]);
			walkTarget[2] = atol(argv[at + 3]);
			walkTargetGiven = true;
			at += 3;
		}
		else if (!strcmp(argv[at], "-walkfinal") ||
			!strcmp(argv[at], "-walkdoors"))
		{
			// -walkdoors shares this branch: the chain is at the nesting limit.
			if (!strcmp(argv[at], "-walkdoors"))
				walkoff::g_door_print = true;
			else
				walkFinal = true;
		}
		else if ((!strcmp(argv[at], "-walkshut") ||
			!strcmp(argv[at], "-flip")) && at + 1 < argc)
		{
			const bool flip = !strcmp(argv[at], "-flip");
			const char* q = argv[++at];

			// -flip shares this branch: the chain is at the nesting limit.
			if (flip)
				for (; *q; )
				{
					walkoff::g_flip_rooms.push_back(atoi(q));

					while (*q && *q != ',')
						q++;

					if (*q)
						q++;
				}
			else if (!strcmp(q, "all"))
				walkoff::g_shut_items.push_back(-1);
			else
				for (; *q; q++)
				{
					walkoff::g_shut_items.push_back(atoi(q));

					while (*q && *q != ',')
						q++;

					if (!*q)
						break;
				}
		}
		else if (!strcmp(argv[at], "-walkdump") && at + 1 < argc)
		{
			walkDump = argv[++at];

			if (at + 1 < argc && argv[at + 1][0] != '-')
				walkDumpFrame = atol(argv[++at]);
		}
		else if (!strcmp(argv[at], "-walkeach"))
			walkEach = true;
		else if (!strcmp(argv[at], "-walkcentre") ||
			!strcmp(argv[at], "-walkcenter"))
			walkCentre = true;
		else if (!strcmp(argv[at], "-reach"))
			walkReach = true;
		else if (!strcmp(argv[at], "-walkboxes"))
			walkBoxes = true;
		else if (!strcmp(argv[at], "-reachsurvey"))
		{
			reachSurvey = true;
			walkReach = true;

			if (at + 1 < argc && argv[at + 1][0] != '-')
				reachStep = atoi(argv[++at]);
		}
		else if (!strcmp(argv[at], "-tround") && at + 2 < argc)
		{
			g_tround_group = atoi(argv[at + 1]);
			sscanf(argv[at + 2], "%ld,%ld,%ld,%ld", &g_tround_box[0],
				&g_tround_box[1], &g_tround_box[2], &g_tround_box[3]);
			at += 2;
		}
		else if (!strcmp(argv[at], "-clipgroup") && at + 2 < argc)
		{
			g_clip_group = atoi(argv[at + 1]);
			sscanf(argv[at + 2], "%ld,%ld,%ld,%ld", &g_clip_box[0],
				&g_clip_box[1], &g_clip_box[2], &g_clip_box[3]);
			at += 2;
		}
		else if (!strcmp(argv[at], "-window") && at + 1 < argc)
		{
			g_window_mode = !strcmp(argv[at + 1], "open") ? HD_WINDOW_OPEN
				: !strcmp(argv[at + 1], "intruding") ? HD_WINDOW_INTRUDING
				: HD_WINDOW_ALL;
			at += 1;
		}
		else if (!strcmp(argv[at], "-roomwin") && at + 1 < argc)
		{
			const char* q = argv[at + 1];

			while (*q)
			{
				int g = -1;
				long w[4];

				if (sscanf(q, "%d:%ld,%ld,%ld,%ld", &g, &w[0], &w[1], &w[2],
					&w[3]) == 5)
				{
					if (g_roomwin_count >= kWithMax)
					{
						printf("-roomwin: more than %d groups - the picture "
							"would leave windows out, so nothing is drawn\n",
							kWithMax);
						return 1;
					}

					memcpy(g_roomwin_box[g_roomwin_count], w, sizeof(w));
					g_roomwin_group[g_roomwin_count++] = g;
				}

				const char* next = strchr(q, ';');

				if (!next)
					break;

				q = next + 1;
			}

			at += 1;
		}
		else if (!strcmp(argv[at], "-unlinked") && at + 1 < argc)
		{
			g_unlinked = atoi(argv[at + 1]);
			g_unlinked = g_unlinked < 0 ? 0 : g_unlinked > 2 ? 2 : g_unlinked;
			printf("-unlinked: art in the air of a room its own room has no "
				"link with is %s\n", g_unlinked == 2
				? "drawn only inside its own window, but where that window is "
				"empty, which keeps the smaller box's answer"
				: g_unlinked
				? "drawn only inside its own window (the engine's)"
				: "answered as if it stood anywhere else");
			at += 1;
		}
		else if (!strcmp(argv[at], "-air") && at + 1 < argc)
		{
			g_air_option = atoi(argv[at + 1]);
			g_air_option = g_air_option == 0 || g_air_option == 2
				? g_air_option : 1;
			printf("-air: the rooms' air is %s\n", g_air_option == 2
				? "handed to the build with the hulls' (tomb5.hd_air 2)"
				: g_air_option
				? "handed to the build (tomb5.hd_air 1, the engine's default)"
				: "not handed to the build (tomb5.hd_air 0)");
			at += 1;
		}
		else if (!strcmp(argv[at], "-decide") && at + 1 < argc)
		{
			g_decide = !strcmp(argv[at + 1], "whole") ? 0
				: !strcmp(argv[at + 1], "scissor") ? 1
				: !strcmp(argv[at + 1], "face") ? 3 : 2;
			at += 1;
		}
		else if (!strcmp(argv[at], "-cull") && at + 1 < argc)
		{
			HDRenderCullOpaque(strcmp(argv[at + 1], "none") ? 1 : 0);
			printf("-cull: the opaque ranges are drawn %s\n",
				strcmp(argv[at + 1], "none") ? "one-sided (the renderer's own)"
					: "two-sided");
			at += 1;
		}
		else if (!strcmp(argv[at], "-flatart") && at + 5 < argc)
		{
			if (g_flat_count < 8)
			{
				for (int c = 0; c < 5; c++)
					g_flat_box[g_flat_count][c] = atoi(argv[at + 1 + c]);

				g_flat_count++;
			}

			at += 5;
		}
		else if (!strcmp(argv[at], "-hasgeom") && at + 6 < argc)
		{
			if (g_geom_count < 8)
			{
				for (int c = 0; c < 6; c++)
					g_geom_box[g_geom_count][c] = (float)atof(argv[at + 1 + c]);

				g_geom_count++;
			}

			at += 6;
		}
		else if (!strcmp(argv[at], "-sky"))
			sky = true;
		else if (!strcmp(argv[at], "-skyonly"))
		{
			sky = true;
			skyOnly = true;
		}
		else if (!strcmp(argv[at], "-animtex"))
			animTex = true;
		else if (!strcmp(argv[at], "-shimmer"))
			g_shimmer = true;
		else if (!strcmp(argv[at], "-wibble") && at + 1 < argc)
		{
			g_wibble = atof(argv[++at]);
			wibbleGiven = true;
		}
		else if (!strcmp(argv[at], "-effect") && at + 1 < argc)
		{
			g_mesh_effect = atoi(argv[++at]);

			if (g_mesh_effect < 0 || g_mesh_effect > 21)
				g_mesh_effect = 0;
		}
		else if (!strcmp(argv[at], "-light") && at + 7 < argc)
		{
			if (g_light_count < HD_MAX_LIGHTS)
			{
				HDLightSource& l = g_lights[g_light_count++];

				l.x = (float)atof(argv[at + 1]);
				l.y = (float)atof(argv[at + 2]);
				l.z = (float)atof(argv[at + 3]);
				l.r = (float)atof(argv[at + 4]);
				l.g = (float)atof(argv[at + 5]);
				l.b = (float)atof(argv[at + 6]);
				l.falloff = (float)atof(argv[at + 7]);
			}
			else
				printf("RoomProbe: more than %d lights - the extra one is dropped, "
					"as the engine drops it\n", HD_MAX_LIGHTS);

			at += 7;
		}
		else if (!strcmp(argv[at], "-bulb") && at + 5 < argc)
		{
			const bool coloured = at + 8 < argc && argv[at + 6][0] != '-';

			if (g_bulb_count < HD_MAX_BULBS)
			{
				HDFogBulb& b = g_bulbs[g_bulb_count++];

				b.x = (float)atof(argv[at + 1]);
				b.y = (float)atof(argv[at + 2]);
				b.z = (float)atof(argv[at + 3]);
				b.rad = (float)atof(argv[at + 4]);
				b.density = (float)atof(argv[at + 5]);
				b.r = coloured ? (float)atof(argv[at + 6]) : 1.0f;
				b.g = coloured ? (float)atof(argv[at + 7]) : 1.0f;
				b.b = coloured ? (float)atof(argv[at + 8]) : 1.0f;
			}
			else
				printf("RoomProbe: more than %d fog bulbs - the extra one is "
					"dropped\n", HD_MAX_BULBS);

			at += coloured ? 8 : 5;
		}
		else if (!strcmp(argv[at], "-fog") && at + 5 < argc)
		{
			g_fog_start = atof(argv[at + 1]);
			printf("RoomProbe: fog start %.0f world units. TR5 has no fog end "
				"and no fog colour - end %s and colour %s/%s/%s are read and "
				"not used (drawroom.cpp:519 subtracts a ramp of 255 over the "
				"start distance again, from the vertex colour).\n",
				g_fog_start, argv[at + 2], argv[at + 3], argv[at + 4], argv[at + 5]);
			at += 5;
		}
		else if (!strcmp(argv[at], "-budget") && at + 1 < argc)
		{
			budgetMiB = atof(argv[at + 1]);
			at += 1;
		}
		else if (!strcmp(argv[at], "-format") && at + 1 < argc)
		{
			pageFormat = !_stricmp(argv[at + 1], "dxt5")
				? HD_PAGE_FORMAT_DXT5 : HD_PAGE_FORMAT_RGBA;
			at += 1;
		}
		else if (!strcmp(argv[at], "-psnr"))
			psnr = true;
		else if (!strcmp(argv[at], "-caps") && at + 1 < argc &&
			!strcmp(argv[at + 1], "noidx32"))
		{
			capNoIndex32 = 1;
			at += 1;
		}
		else if (!strcmp(argv[at], "-caps") && at + 2 < argc &&
			!strcmp(argv[at + 1], "maxtex"))
		{
			capMaxTex = atoi(argv[at + 2]);
			at += 2;
		}
		else if (!strcmp(argv[at], "-seq") && at + 1 < argc)
		{
			sequence = argv[at + 1];
			at += 1;
		}
		// -vramhog and -nowarm share a branch: the chain is at the
		// compiler's depth.
		else if (!strcmp(argv[at], "-vramhog") || !strcmp(argv[at], "-nowarm"))
			(argv[at][1] == 'v' ? vramHog : g_nowarm) = true;
		else
			HDDrawnSwitch(argc, argv, at);
	}

	/* One knob for all the motion. -time is the game clock the scroll and
	 * wind ride on; the water wobble and the shimmer ride on the engine's
	 * wibble counter, which the engine steps by four per game frame at 30 Hz
	 * and wraps at 256 (control.cpp). So unless -wibble says otherwise,
	 * -time sets that counter to the value the engine would have at that
	 * second, and two -time values are two moments of the whole picture
	 * rather than of half of it. */
	if (!wibbleGiven)
		g_wibble = (double)(((int)(g_time * 30.0) * 4) & 0xFC);

	printf("RoomProbe - %s room %d, %s pages, %d decoding threads\n",
		stem, room, managed ? "MANAGED" : "DEFAULT", threads);
	printf("install: %s\n", root);
	printf("commit before anything: %.1f MiB\n", CommitMiB());

	// -skycensus needs no device: the two files, read and printed.
	if (g_sky_census)
		return ProbeSkyCensus(root, stem, rooms);

	/* -walk needs no device, no .TRG and no picture - only the classic
	 * level - so it runs here, before the window exists. */

	if (reachSurvey)
	{
		if (g_roombox.empty())
		{
			printf("-reachsurvey needs -roombox <folder holding the classic "
				".TRC>\n");
			return 5;
		}

		return RunReachSurvey(g_roombox, stem, walkYaw, walkPitch, width,
			height, fov, reachStep);
	}

	if (walkDump)
	{
		if (g_roombox.empty())
		{
			printf("-walkdump needs -roombox <folder holding the .TRC files "
				"that were played>\n");
			return 5;
		}

		return RunWalkDump(g_roombox, walkDump, walkDumpFrame, walkFinal);
	}

	if (walkSeed >= 0)
	{
		if (g_roombox.empty())
		{
			printf("-walk needs -roombox <folder holding the classic .TRC>\n");
			return 5;
		}

		return RunWalk(g_roombox, stem, walkSeed, walkEye[0], walkEye[1],
			walkEye[2], walkYaw, walkPitch,
			walkWant, width, height, fov, walkEach, walkReach, walkCentre,
			walkBoxes, root, walkTargetGiven ? walkTarget : 0, walkFinal);
	}

	/* -walktarget without -walk: the picture's camera is the engine's. The
	 * eye is the room-local x y z of the command line, the target a world
	 * point as ROOMLIST prints it, so the seed room's origin is read out of
	 * the classic level; the yaw, the pitch and the perspective become the
	 * integer ones phd_LookAt and AlterFOV make of them, and LookAt below
	 * builds the view from those. -decide frame holds the classic faces
	 * against the same view. */
	double enginePersp = 0.0;

	if (walkTargetGiven)
	{
		if (g_roombox.empty() || !haveCamera || !walkoff::Load(g_roombox, stem) ||
			room < 0 || room >= (int)walkoff::g_room.size())
		{
			printf("-walktarget without -walk needs the camera x y z, -roombox "
				"and a room of that level\n");
			return 5;
		}

		const walkoff::Room& r = walkoff::g_room[(size_t)room];
		const long ex = r.x + (long)floor(cx + 0.5);
		const long ey = r.y + (long)floor(cy + 0.5);
		const long ez = r.z + (long)floor(cz + 0.5);
		short angles[2];

		enginecam::VectorAngles(walkTarget[0] - ex, walkTarget[1] - ey,
			walkTarget[2] - ez, angles);
		yaw = angles[0] * 360.0 / 65536.0;
		pitch = -angles[1] * 360.0 / 65536.0;
		enginePersp = (double)enginecam::Persp(height,
			enginecam::FovUnits(fov));
		printf("-walktarget: eye %ld/%ld/%ld target %ld/%ld/%ld  y_rot %d "
			"x_rot %d (yaw %.4f pitch %.4f deg)  persp %.0f\n", ex, ey, ez,
			walkTarget[0], walkTarget[1], walkTarget[2], (int)angles[0],
			(int)angles[1], yaw, pitch, enginePersp);
	}

	if (!MakeDevice())
	{
		DropDevice();
		return 2;
	}

	const UINT vramBefore = g_device->GetAvailableTextureMem();
	const char* why = "";

	/* The small card, before anything reads the device's caps. HDRenderInit
	 * copies the caps once and everything after it reads that copy, so the
	 * overrides have to be in place first. */
	if (capMaxTex || capNoIndex32)
	{
		printf("caps override: max texture %s, 32 bit index buffer %s\n",
			capMaxTex ? "forced down" : "as the device says",
			capNoIndex32 ? "FORCED OFF" : "as the device says");

		if (capMaxTex)
			printf("  MaxTextureWidth/Height := %d\n", capMaxTex);

		HDRenderCapOverride(capMaxTex, capNoIndex32);
	}

	// The build's fingerprint is always on in the tool (HDRenderBuildHash).
	HDRenderBuildHash(1);

	if (!HDRenderInit(g_device, &why))
	{
		printf("RoomProbe: HDRenderInit failed - %s\n", why);
		DropDevice();
		return 3;
	}

	// -nocopy, -nodepth, -waterdepthview.
	HDRenderWaterStage2(g_water_copy ? 1 : 0, g_water_depth ? 1 : 0,
		g_water_depth_view, g_water_bend);
	printf("water depth texture: INTZ %s by the adapter%s\n",
		HDRenderWaterDepthOffered() ? "offered" : "NOT offered",
		g_water_depth ? "" : " (-nodepth: not used)");

	/* The texture policy. The device's own size limit is added inside
	 * HDRenderLevelBegin, exactly as it is in the game - this sets only the
	 * two switches this tool exposes: format and budget. */
	{
		HDPageCachePolicy policy;

		policy.format = pageFormat;
		policy.budgetBytes = budgetMiB > 0 ? budgetMiB * 1048576.0 : 0.0;
		policy.maxSide = 0;
		HDPageCacheConfigure(&policy);

		printf("pages: format %s, budget %s\n",
			pageFormat == HD_PAGE_FORMAT_DXT5 ? "DXT5" : "A8R8G8B8",
			budgetMiB > 0 ? "" : "none");

		if (budgetMiB > 0)
			printf("  budget %.0f MiB\n", budgetMiB);
	}

	/* The card, filled up. 4096x4096 A8R8G8B8 is 64 MiB a block, taken in
	 * D3DPOOL_DEFAULT so it really lives on the card, until CreateTexture
	 * says no. Nothing is ever released - the process ends and Direct3D
	 * takes it all back - because the point is to leave the card full while
	 * the level loads. */
	std::vector<IDirect3DTexture9*> hogs;

	if (vramHog)
	{
		printf("\n-- filling the card -----------------------------------------\n");
		printf("free texture memory before: %.0f MiB\n",
			(double)g_device->GetAvailableTextureMem() / 1048576.0);

		for (;;)
		{
			IDirect3DTexture9* block = 0;

			if (FAILED(g_device->CreateTexture(4096, 4096, 1, 0, D3DFMT_A8R8G8B8,
				D3DPOOL_DEFAULT, &block, 0)) || !block)
				break;

			hogs.push_back(block);
		}

		printf("%d block(s) of 64 MiB taken, %.0f MiB of texture memory left\n",
			(int)hogs.size(), (double)g_device->GetAvailableTextureMem() / 1048576.0);
	}

	PrintDepthTruth("(device fresh)");

	if (sequence)
	{
		RunSequence(root, sequence, rooms, managed, threads);
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	HDRenderStats stats;
	const double commitBefore = CommitMiB();

	// The synthetic surface marks of -plane, borrowed for the length of the
	// build exactly as the engine lends its own.
	if (g_plane)
		HDRenderSetWaterMarks(g_plane_marks, 4);

	// -tround needs the overhang index layout, and nothing else does.
	if (g_tround_group >= 0)
		HDRenderSplitOverhang(1);

	if ((g_loadtime || g_ahead_pics) && !g_roombox.empty())
	{
		HDPageCacheAheadVerify(g_ahead_verify);
		LoadTimeAheadStart(root, stem, threads);
	}

	LoadTimeStep(stem, 0);

	/* The rooms' own boxes out of the classic level, the same arithmetic
	 * game\draw.cpp uses and hd_level.cpp copies. */
	if (!g_roombox.empty())
	{
		Tr5Level level;
		std::string error;
		const std::string trc = g_roombox + "\\" + stem + ".TRC";
		std::wstring wide;

		for (size_t i = 0; i < trc.size(); i++)
			wide.push_back((wchar_t)(unsigned char)trc[i]);

		if (!level.load(wide, error))
			printf("RoomProbe: -roombox could not read %s (%s)\n",
				trc.c_str(), error.c_str());
		else
		{
			LoadTimeStep(stem, "classic_read");
			LoadTimeScan(root, stem, level);
			g_boxes_built.resize((size_t)level.roomCount());

			for (int i = 0; i < level.roomCount(); i++)
			{
				const Tr5Room& rm = level.rooms()[(size_t)i];
				HDRoomBox& b = g_boxes_built[(size_t)i];
				unsigned yTop = 0, yOrigin = 0;

				level.headerField(i, 24, yOrigin, error);
				level.headerField(i, 36, yTop, error);

				b.mins[0] = 1024.0f;
				b.maxs[0] = (float)((rm.numXSectors - 1) * 1024);
				b.mins[1] = (float)((int)yTop - (int)yOrigin);
				b.maxs[1] = (float)(rm.yBottom - (int)yOrigin);
				b.mins[2] = 1024.0f;
				b.maxs[2] = (float)((rm.numZSectors - 1) * 1024);

				for (int axis = 0; axis < 3; axis++)
					if (b.maxs[axis] < b.mins[axis])
						b.maxs[axis] = b.mins[axis];

				b.originX = (float)rm.originX;
				b.originZ = (float)rm.originZ;
			}

			HDRenderSetRoomBoxes(&g_boxes_built[0],
				(int)g_boxes_built.size());

			/* -watermarks: the marks hd_level.cpp's BuildWaterMarks hands
			 * in, and -plane's after them, borrowed for the build as the
			 * engine lends its own. */
			if (g_watermarks)
			{
				const int marked = probemarks::Build(level);
				std::vector<HDWaterMark>& marks = probemarks::g_marks;

				printf("-watermarks: level=%s water marks=%d rooms=%d "
					"doors_shut=%d\n", stem, (int)marks.size(), marked,
					probemarks::g_shut);

				if (g_plane)
					marks.insert(marks.end(), g_plane_marks,
						g_plane_marks + 4);

				HDRenderSetWaterMarks(marks.empty() ? 0 : &marks[0],
					(int)marks.size());
			}
			printf("-roombox: %d room boxes handed in from %s\n",
				(int)g_boxes_built.size(), trc.c_str());

			LoadTimeStep(stem, "boxes");

			/* And the floor squares, as hd_level.cpp's BuildRoomSectors
			 * hands them in: the light cubes and the lamps of the sheen, and
			 * the seed of its page classes, come out of them. Read in the
			 * file's own order, x-major, which is the order of
			 * ROOM_INFO::floor. */
			g_grids_built.resize((size_t)level.roomCount());
			g_sectors_built.clear();

			for (int i = 0; i < level.roomCount(); i++)
			{
				const Tr5Room& rm = level.rooms()[(size_t)i];
				std::vector<Tr5Level::SectorInfo> squares;
				HDRoomGrid& grid = g_grids_built[(size_t)i];

				grid.first = (int)g_sectors_built.size();
				grid.xSectors = 0;
				grid.zSectors = 0;
				grid.originX = (float)rm.originX;
				grid.originZ = (float)rm.originZ;

				if (!level.sectorInfo(i, squares, error) ||
					squares.size() != (size_t)rm.numXSectors * rm.numZSectors)
					continue;

				grid.xSectors = rm.numXSectors;
				grid.zSectors = rm.numZSectors;

				for (size_t s = 0; s < squares.size(); s++)
				{
					HDSectorFloor f;

					f.floorY = (short)(squares[s].floor * 256);
					f.fx = (unsigned char)(squares[s].box & 15);
					f.wall = squares[s].floor == -127 ? 1 : 0;
					g_sectors_built.push_back(f);
				}
			}

			if (!g_sectors_built.empty())
				HDRenderSetSectors(&g_grids_built[0], (int)g_grids_built.size(),
					&g_sectors_built[0], (int)g_sectors_built.size());

			printf("-roombox: %d floor squares handed in\n",
				(int)g_sectors_built.size());

			/* -fieldsight: the rooms within two portals of each room, a
			 * flipmap twin counting as the room itself - the table TrgProbe
			 * -glossfield measures the field's leak with. */
			if (g_field_sight)
			{
				const int rooms = level.roomCount();
				std::vector<unsigned char> nearby((size_t)rooms * rooms, 0);
				std::vector<std::vector<int> > twins((size_t)rooms);

				for (int i = 0; i < rooms; i++)
				{
					const int alt = level.rooms()[(size_t)i].altRoom;

					if (alt >= 0 && alt < rooms)
					{
						twins[(size_t)i].push_back(alt);
						twins[(size_t)alt].push_back(i);
					}
				}

				for (int i = 0; i < rooms; i++)
				{
					std::vector<int> front(1, i);

					front.insert(front.end(), twins[(size_t)i].begin(), twins[(size_t)i].end());

					for (const int f : front)
						nearby[(size_t)i * rooms + f] = 1;

					for (int step = 0; step < 2; step++)
					{
						std::vector<int> next;

						for (const int at : front)
							for (const int to : level.rooms()[(size_t)at].portalRooms)
							{
								if (to < 0 || to >= rooms)
									continue;

								std::vector<int> add(1, to);

								add.insert(add.end(), twins[(size_t)to].begin(), twins[(size_t)to].end());

								for (const int a : add)
									if (!nearby[(size_t)i * rooms + a])
									{
										nearby[(size_t)i * rooms + a] = 1;
										next.push_back(a);
									}
							}

						front.swap(next);
					}
				}

				HDRenderGlossFieldSight(&nearby[0], rooms);
				printf("-fieldsight: the light field takes the sight test by the rooms\n");
			}

			LoadTimeStep(stem, "sectors");

			/* And the rooms that may hold another room's art. The rule is
			 * BuildRoomIntrusions' (specific\hd\hd_level.cpp), read out of
			 * the same file: not next door by portal or by a vertical floor
			 * link, not a flipmap twin, boxes touching, and the holder's own
			 * inside, TR5's box one sector in from the grid's edge. */
			{
				const int n = level.roomCount();
				std::vector<std::set<int> > nextDoor((size_t)n);

				for (int r = 0; r < n; r++)
				{
					const std::vector<int>& ports =
						level.rooms()[(size_t)r].portalRooms;

					for (size_t p = 0; p < ports.size(); p++)
						if (ports[p] >= 0 && ports[p] < n)
						{
							nextDoor[(size_t)r].insert(ports[p]);
							nextDoor[(size_t)ports[p]].insert(r);
						}

					std::vector<Tr5Level::SectorInfo> sect;

					if (!level.sectorInfo(r, sect, error))
						continue;

					for (size_t q = 0; q < sect.size(); q++)
					{
						const int up = sect[q].roomAbove;
						const int dn = sect[q].roomBelow;

						if (up >= 0 && up < n)
						{
							nextDoor[(size_t)r].insert(up);
							nextDoor[(size_t)up].insert(r);
						}

						if (dn >= 0 && dn < n)
						{
							nextDoor[(size_t)r].insert(dn);
							nextDoor[(size_t)dn].insert(r);
						}
					}
				}

				for (int a = 0; a < n; a++)
				{
					const Tr5Room& ra = level.rooms()[(size_t)a];
					unsigned aTop = 0, aOrigin = 0;

					level.headerField(a, 24, aOrigin, error);
					level.headerField(a, 36, aTop, error);

					for (int b = 0; b < n; b++)
					{
						if (a == b || nextDoor[(size_t)a].count(b))
							continue;

						if (ra.altRoom == b ||
							level.rooms()[(size_t)b].altRoom == a)
							continue;

						const Tr5Room& rb = level.rooms()[(size_t)b];
						unsigned bTop = 0, bOrigin = 0;

						level.headerField(b, 24, bOrigin, error);
						level.headerField(b, 36, bTop, error);

						if (ra.originX + (long)ra.numXSectors * 1024 <= rb.originX ||
							rb.originX + (long)rb.numXSectors * 1024 <= ra.originX ||
							ra.originZ + (long)ra.numZSectors * 1024 <= rb.originZ ||
							rb.originZ + (long)rb.numZSectors * 1024 <= ra.originZ ||
							ra.yBottom <= (long)(int)bTop ||
							rb.yBottom <= (long)(int)aTop)
							continue;

						HDIntrusion box;

						box.group = b;
						box.holder = a;
						box.mins[0] = (float)(ra.originX + 1024 - rb.originX);
						box.maxs[0] = (float)(ra.originX +
							(long)(ra.numXSectors - 1) * 1024 - rb.originX);
						box.mins[1] = (float)((long)(int)aTop - (long)(int)bOrigin);
						box.maxs[1] = (float)(ra.yBottom - (long)(int)bOrigin);
						box.mins[2] = (float)(ra.originZ + 1024 - rb.originZ);
						box.maxs[2] = (float)(ra.originZ +
							(long)(ra.numZSectors - 1) * 1024 - rb.originZ);

						if (box.maxs[0] <= box.mins[0] ||
							box.maxs[2] <= box.mins[2])
							continue;

						g_intrusions_built.push_back(box);
					}
				}

				if (!g_intrusions_built.empty())
					HDRenderSetIntrusions(&g_intrusions_built[0],
						(int)g_intrusions_built.size());

				printf("-roombox: %d intrusion boxes handed in\n",
					(int)g_intrusions_built.size());

				LoadTimeStep(stem, "intr");

				/* And every room's air, as BuildRoomAir hands it over: per
				 * floor square its floor and ceiling bytes and its floor
				 * data words from its index on, read by HDSectorAir. None
				 * with -air 0, as none with tomb5.hd_air 0; the hulls with
				 * -air 2, as with tomb5.hd_air 2. */
				HDRenderSetAirHulls(g_air_option == 2 ? 1 : 0);

				if (g_air_option)
				{
					LARGE_INTEGER from, to, rate;
					std::vector<size_t> firstAt((size_t)n, 0);

					QueryPerformanceCounter(&from);
					g_air_built.assign((size_t)n, HDRoomAir());
					g_air_sectors_built.clear();
					g_air_links_built.assign((size_t)n * (size_t)n, 0);

					for (int r = 0; r < n; r++)
					{
						const Tr5Room& rm = level.rooms()[(size_t)r];
						HDRoomAir& air = g_air_built[(size_t)r];
						std::vector<Tr5Level::SectorInfo> sect;
						unsigned origin = 0;

						level.headerField(r, 24, origin, error);
						air.x = (float)rm.originX;
						air.y = (float)(int)origin;
						air.z = (float)rm.originZ;
						air.nx = rm.numXSectors;
						air.nz = rm.numZSectors;
						air.sectors = 0;
						firstAt[(size_t)r] = g_air_sectors_built.size();

						const size_t squares =
							(size_t)(air.nx > 0 ? air.nx : 0) *
							(size_t)(air.nz > 0 ? air.nz : 0);

						for (size_t q = 0; q < squares; q++)
						{
							g_air_sectors_built.push_back(HD_AIR_NONE);
							g_air_sectors_built.push_back(-HD_AIR_NONE);
						}

						if (!level.sectorInfo(r, sect, error))
							continue;

						for (size_t q = 0; q < sect.size(); q++)
						{
							const Tr5Level::SectorInfo& si = sect[q];
							const size_t at = firstAt[(size_t)r] +
								((size_t)si.sx * air.nz + si.sz) * 2;
							std::vector<Tr5FloorEntry> chain;
							std::vector<unsigned short> words;

							if (si.fdIndex &&
								!level.readChain(si.fdIndex, chain))
								continue;	// unreadable: no air

							for (size_t e = 0; e < chain.size(); e++)
								words.insert(words.end(),
									chain[e].words.begin(),
									chain[e].words.end());

							int door = -1;

							if (at + 1 < g_air_sectors_built.size())
								HDSectorAir(si.floor, si.ceiling,
									words.empty() ? 0 : &words[0],
									(int)words.size(),
									&g_air_sectors_built[at],
									&g_air_sectors_built[at + 1], &door);

							if (door >= 0 && door < n)
							{
								g_air_links_built[(size_t)r * n + door] = 1;
								g_air_links_built[(size_t)door * n + r] = 1;
							}
						}
					}

					for (int r = 0; r < n; r++)
						if (g_air_built[(size_t)r].nx > 0 &&
							g_air_built[(size_t)r].nz > 0)
							g_air_built[(size_t)r].sectors =
								&g_air_sectors_built[firstAt[(size_t)r]];

					/* The pairs not asked, row by the art's room: the same
					 * room, next door, a floor portal (above), flipmap
					 * twins. */
					for (int b = 0; b < n; b++)
						for (int a = 0; a < n; a++)
							if (a == b || nextDoor[(size_t)b].count(a) ||
								level.rooms()[(size_t)a].altRoom == b ||
								level.rooms()[(size_t)b].altRoom == a)
								g_air_links_built[(size_t)b * n + a] = 1;

					// Closed over the flipmap twins, as BuildRoomAir does.
					{
						std::vector<int> twin((size_t)n, -1);

						for (int r = 0; r < n; r++)
						{
							const int alt = level.rooms()[(size_t)r].altRoom;

							if (alt >= 0 && alt < n)
							{
								twin[(size_t)r] = alt;
								twin[(size_t)alt] = r;
							}
						}

						const std::vector<unsigned char> direct =
							g_air_links_built;

						for (int b = 0; b < n; b++)
							for (int a = 0; a < n; a++)
							{
								const int tb = twin[(size_t)b];
								const int ta = twin[(size_t)a];

								if ((ta >= 0 && direct[(size_t)b * n + ta]) ||
									(tb >= 0 && direct[(size_t)tb * n + a]) ||
									(ta >= 0 && tb >= 0 &&
									direct[(size_t)tb * n + ta]))
									g_air_links_built[(size_t)b * n + a] = 1;
							}
					}

					QueryPerformanceCounter(&to);
					QueryPerformanceFrequency(&rate);
					g_air_ms = rate.QuadPart ? (double)(to.QuadPart -
						from.QuadPart) * 1000.0 / (double)rate.QuadPart : 0.0;

					if (n > 0)
						HDRenderSetRoomAir(&g_air_built[0], n,
							&g_air_links_built[0]);

					/* And the rooms under the sky with the flipmap twins,
					 * as BuildRoomCarry hands them over. */
					static std::vector<unsigned char> sky;
					static std::vector<short> twin;

					sky.assign((size_t)n, 0);
					twin.assign((size_t)n, -1);

					for (int r = 0; r < n; r++)
					{
						sky[(size_t)r] = (level.rooms()[(size_t)r].flags &
							0x8) ? 1 : 0;
						twin[(size_t)r] =
							(short)level.rooms()[(size_t)r].altRoom;
					}

					if (n > 0)
						HDRenderSetCarryHosts(&sky[0], &twin[0], n);
				}

				LoadTimeStep(stem, "air");

				/* And the intruders' classic faces, as BuildPairFaces hands
				 * them over: every drawn face of every intruder room, in
				 * the room's own frame, in the roomlets' order, the ones
				 * that stand further than HD_PAIR_FACE_REACH from every
				 * box of that room left out. */
				for (int b = 0; b < n; b++)
				{
					bool intruder = false;

					for (size_t k = 0; k < g_intrusions_built.size(); k++)
						if (g_intrusions_built[k].group == b)
							intruder = true;

					std::vector<Tr5Level::FaceInfo> faces;

					if (!intruder || !level.roomFaces(b, faces, error))
						continue;

					const Tr5Room& rb = level.rooms()[(size_t)b];

					for (size_t f = 0; f < faces.size(); f++)
					{
						HDPairFace face;
						float lo[3], hi[3];

						face.group = b;
						face.corners = faces[f].quad ? 4 : 3;

						for (int c = 0; c < 4; c++)
						{
							const int at = c < face.corners ? c : 0;

							face.v[c][0] = (float)(faces[f].v[at][0] - rb.originX);
							face.v[c][1] = (float)faces[f].v[at][1];
							face.v[c][2] = (float)(faces[f].v[at][2] - rb.originZ);
						}

						for (int axis = 0; axis < 3; axis++)
						{
							lo[axis] = hi[axis] = face.v[0][axis];

							for (int c = 1; c < face.corners; c++)
							{
								if (face.v[c][axis] < lo[axis]) lo[axis] = face.v[c][axis];
								if (face.v[c][axis] > hi[axis]) hi[axis] = face.v[c][axis];
							}
						}

						bool nearBox = false;

						for (size_t k = 0; k < g_intrusions_built.size() && !nearBox; k++)
						{
							const HDIntrusion& box = g_intrusions_built[k];

							if (box.group != b)
								continue;

							nearBox = true;

							for (int axis = 0; axis < 3; axis++)
								if (hi[axis] + (float)HD_PAIR_FACE_REACH < box.mins[axis] ||
									lo[axis] - (float)HD_PAIR_FACE_REACH > box.maxs[axis])
									nearBox = false;
						}

						if (nearBox)
							g_faces_built.push_back(face);
					}
				}

				if (!g_faces_built.empty())
					HDRenderSetPairFaces(&g_faces_built[0],
						(int)g_faces_built.size());

				printf("-roombox: %d classic faces handed in\n",
					(int)g_faces_built.size());

				LoadTimeStep(stem, "pairs");

				/* And the faces another group's art may stand on, as
				 * BuildStandFaces hands them over: every room's origin and
				 * twin, every room with a classic face that -hdm does not
				 * name whole, its whole box grown by HD_COVER_REACH, the
				 * faces in world units. */
				g_stand_rooms_built.resize((size_t)n);

				for (int r = 0; r < n; r++)
				{
					const Tr5Room& rm = level.rooms()[(size_t)r];
					unsigned yOrigin = 0;

					level.headerField(r, 24, yOrigin, error);
					g_stand_rooms_built[(size_t)r].origin[0] = (float)rm.originX;
					g_stand_rooms_built[(size_t)r].origin[1] = (float)(int)yOrigin;
					g_stand_rooms_built[(size_t)r].origin[2] = (float)rm.originZ;
					g_stand_rooms_built[(size_t)r].twin = rm.altRoom;
				}

				for (int r = 0; r < n; r++)
				{
					const Tr5Room& rm = level.rooms()[(size_t)r];
					std::vector<Tr5Level::FaceInfo> faces;
					unsigned yTop = 0;

					if ((g_hdm && HDMarkersRoomTouched(r)) ||
						!level.roomFaces(r, faces, error) || faces.empty())
						continue;

					level.headerField(r, 36, yTop, error);

					HDStandHolder holder;

					holder.room = r;
					holder.firstFace = (int)g_stand_faces_built.size();
					holder.faceCount = (int)faces.size();
					holder.mins[0] = (float)(rm.originX - HD_COVER_REACH);
					holder.maxs[0] = (float)(rm.originX + (long)rm.numXSectors * 1024 +
						HD_COVER_REACH);
					holder.mins[1] = (float)((int)yTop - HD_COVER_REACH);
					holder.maxs[1] = (float)(rm.yBottom + HD_COVER_REACH);
					holder.mins[2] = (float)(rm.originZ - HD_COVER_REACH);
					holder.maxs[2] = (float)(rm.originZ + (long)rm.numZSectors * 1024 +
						HD_COVER_REACH);

					for (const Tr5Level::FaceInfo& face : faces)
					{
						HDCoverFace one;

						one.corners = face.quad ? 4 : 3;
						one.doubleSided = face.doubleSided ? 1 : 0;

						for (int c = 0; c < 4; c++)
							for (int k = 0; k < 3; k++)
								one.v[c][k] = (double)face.v[c < one.corners ? c : 0][k];

						g_stand_faces_built.push_back(one);
					}

					g_stand_holders_built.push_back(holder);
				}

				if (!g_stand_holders_built.empty())
					HDRenderSetStandFaces(&g_stand_rooms_built[0],
						(int)g_stand_rooms_built.size(), &g_stand_holders_built[0],
						(int)g_stand_holders_built.size(), &g_stand_faces_built[0],
						(int)g_stand_faces_built.size());

				printf("-roombox: %d rooms with %d classic faces handed in for "
					"the stand ranges\n", (int)g_stand_holders_built.size(),
					(int)g_stand_faces_built.size());
			}

			LoadTimeStep(stem, "stand");

			/* -hdm: the openings the randomizer cut, out of the sidecar
			 * beside that very .TRC, in the shape hd_level.cpp's
			 * BuildRoomCuts hands them over - one entry per (group, box)
			 * pair, in the group's own frame, every group asked about every
			 * box. The room origins come out of the classic level that is
			 * open here anyway; room[].y is the header's own field 24, the
			 * same one the room boxes above read. */
			if (g_hdm)
			{
				if (!HDMarkersLoad(trc.c_str()))
					printf("-hdm: no markers (%s) - nothing is cut\n",
						HDMarkersError());
				else
				{
					printf("-hdm: %s, version %d, %d rooms, %d cut boxes, "
						"%d donors\n", HDMarkersPath(), HDMarkersVersion(),
						HDMarkersRoomCount(), HDMarkersCutCount(),
						HDMarkersDonorCount());

					for (int i = 0; i < HDMarkersRoomCount(); i++)
						printf("-hdm: room %d is named whole - the engine "
							"draws it CLASSIC and this tool cannot\n",
							HDMarkersRoom(i));

					for (int b = 0; b < HDMarkersCutCount(); b++)
					{
						const HDMarkerCut* cut = HDMarkersCut(b);

						if (!cut)
							continue;

						printf("-hdm: cut %d  x %d..%d  y %d..%d  z %d..%d\n",
							b, cut->mins[0], cut->maxs[0], cut->mins[1],
							cut->maxs[1], cut->mins[2], cut->maxs[2]);

						for (int r = 0; r < level.roomCount(); r++)
						{
							const Tr5Room& rm = level.rooms()[(size_t)r];
							unsigned yOrigin = 0;
							HDCut one;

							level.headerField(r, 24, yOrigin, error);

							one.group = r;
							one.box = b;
							one.mins[0] = (float)(cut->mins[0] - rm.originX);
							one.maxs[0] = (float)(cut->maxs[0] - rm.originX);
							one.mins[1] = (float)(cut->mins[1] - (int)yOrigin);
							one.maxs[1] = (float)(cut->maxs[1] - (int)yOrigin);
							one.mins[2] = (float)(cut->mins[2] - rm.originZ);
							one.maxs[2] = (float)(cut->maxs[2] - rm.originZ);
							g_cuts_built.push_back(one);
						}
					}

					if (!g_cuts_built.empty())
						HDRenderSetCuts(&g_cuts_built[0],
							(int)g_cuts_built.size());

					printf("-hdm: %d (group, box) pairs handed in\n",
						(int)g_cuts_built.size());
				}
			}
		}
	}
	else if (g_hdm)
		printf("RoomProbe: -hdm needs -roombox <folder holding the classic "
			".TRC> - that folder is where the sidecar lives\n");

	if (g_watermarks && g_roombox.empty())
		printf("RoomProbe: -watermarks needs -roombox <folder holding the "
			"classic .TRC> - the marks come out of its rooms\n");

	HDRenderGlossOverride(g_gloss_max ? 1 : 0);
	HDRenderGlossFieldView(g_gloss_field);
	HDRenderGlossFile(g_gloss_file.empty() ? 0 : g_gloss_file.c_str());

	// The statics' places, for their pieces, as hd_level.cpp hands them.
	if (!g_roombox.empty())
		probestatics::HandPlaces(stem);

	LoadTimeStep(stem, "places");

	if (!HDRenderLevelBegin(root, stem, rooms, managed, threads, &stats, &why))
	{
		printf("RoomProbe: HDRenderLevelBegin failed - %s\n", why);

		// The page cache's notes before the shutdown empties them: on the way
		// out they are the only place that says which page it was.
		for (int i = 0; i < HDPageCacheNoteCount(); i++)
			printf("  note: %s\n", HDPageCacheNote(i));

		LoadTimeAheadWait();
		HDRenderShutdown();
		DropDevice();
		return 4;
	}

	LoadTimeStep(stem, "begin_wall");
	LoadTimeBuild(stem, stats);

	if (g_loadtime)
	{
		LoadTimeAheadWait();
		HDPageCacheAheadDrop();

		if (g_reload && !g_roombox.empty())
			LoadTimeReload(root, stem, budgetMiB, pageFormat);

		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	if (g_ahead_pics)
	{
		int ahead = 0, taken = 0;
		double ahead_ms = 0.0;

		HDPageCacheAheadInfo(&ahead, &taken, &ahead_ms);
		printf("-ahead: pages=%d taken=%d trg_kept=%d differ=%d%s\n", ahead,
			taken, HDScanTrgTaken() ? 1 : 0, HDPageCacheAheadDiffer(),
			g_ahead_verify ? "" : " (not verified)");
	}

	/* -reloadas: the other level's build after the first level's, with
	 * what was read and decoded ahead for it taken, and the run's peak. */
	if (g_reload_rebuild)
	{
		int taken = 0;

		HDPageCacheAheadInfo(0, &taken, 0);
		printf("-reload: rebuilt level=%s hash=%016llx trg_kept=%d taken=%d "
			"differ=%d peak_mib=%.1f\n", stem, stats.hash,
			HDScanTrgTaken() ? 1 : 0, taken, HDPageCacheAheadDiffer(),
			PeakCommitMiB());
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	if (g_reload && !ProbeReload(root, stem, rooms, managed, threads,
		budgetMiB, pageFormat))
	{
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	/* -statictable: the answers, and no picture. */
	if (probestatics::g_table)
	{
		probestatics::Table(stem);
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	/* -smashcensus: the pieces and their statics, and no picture. */
	if (g_smash_census)
	{
		probestatics::SmashCensus(stem);
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	const double commitAfter = CommitMiB();
	const UINT vramAfter = g_device->GetAvailableTextureMem();

	printf("\n-- the level ------------------------------------------------\n");

	if (rooms > 0)
		printf(".TRG version %d, %d groups (%d rooms handed in, %d sky, %d empty)\n",
			stats.trgVersion, stats.groups, stats.rooms, stats.skyGroups,
			stats.emptyGroups);
	else
		printf(".TRG version %d, %d groups (no room count given - none counted "
			"as sky), %d empty\n", stats.trgVersion, stats.groups, stats.emptyGroups);
	printf("%d vertices (%.1f MiB), %d indices (%.1f MiB, %d bit), %d ranges%s\n",
		stats.vertices, stats.vertexBytes / 1048576.0,
		stats.indices, stats.indexBytes / 1048576.0, stats.index32 ? 32 : 16,
		stats.ranges, stats.splitRanges ? " (some split by the primitive limit)" : "");
	printf("biggest range %d triangles, %d triangles on the fallback page\n",
		stats.biggestRangeTris, stats.fallbackPageTris);
	{
		int culled = 0, both = 0;

		HDRenderCullRanges(&culled, &both);
		printf("cull: ranges opaque=%d two_sided=%d\n", culled, both);
	}
	/* The overlap list out of the engine's own build, so that it can be held
	 * line for line against TrgProbe -roomspace, which answers the same
	 * question out of the two files without a renderer. The two lists have
	 * to be the same list. */
	if (!g_intrusions_built.empty() || HDRenderAirPairs())
	{
		/* The pieces, counted the way ReportRoomIntrusions counts them for
		 * the engine's own two lines. */
		std::vector<int> pairFaces(g_intrusions_built.size(), 0);
		std::vector<int> pairBound(g_intrusions_built.size(), 0);
		std::set<int> claimed;
		// A face owns up to two pieces of a pair and is counted once.
		std::set<std::pair<int, int> > pairClaimed;
		int bound = 0, unbound = 0;
		// And the air's pieces the engine's line leaves out of its counts.
		int openAir = 0;
		std::set<int> openGroups;

		for (int g = 0; g < HDRenderGroupCount(); g++)
			for (int j = 0; j < HDRenderGroupPairRanges(g); j++)
			{
				HDPairRange piece;

				if (!HDRenderGroupPairRange(g, j, &piece))
					break;

				// Art the air took out of the open half is no intruding art.
				if (piece.face < 0 && !piece.inner)
				{
					openAir += piece.triangles;
					openGroups.insert(g);
					continue;
				}

				if (piece.face < 0)
				{
					unbound += piece.triangles;
					continue;
				}

				bound += piece.triangles;
				claimed.insert(piece.face);

				if (piece.intrusion >= 0 &&
					piece.intrusion < (int)g_intrusions_built.size())
				{
					if (pairClaimed.insert(std::make_pair(piece.intrusion,
						piece.face)).second)
						pairFaces[(size_t)piece.intrusion]++;

					pairBound[(size_t)piece.intrusion] += piece.triangles;
				}
			}

		int band = 0;

		for (size_t i = 0; i < g_intrusions_built.size(); i++)
			band += HDRenderIntrusionBand((int)i);

		/* The air fields of the engine's level line (ReportRoomIntrusions):
		 * air_ms reading the rooms' air here plus the renderer's asking,
		 * and air= the -air this build took. */
		int airTris = 0, airGroups = 0, airBoxed = 0;
		const int airPairs = HDRenderAirPairs();
		int intruding = 0, intrudingGroups = 0;

		for (int g = 0; g < HDRenderGroupCount(); g++)
		{
			int here = HDRenderGroupIntrudingTriangles(g);

			airTris += HDRenderGroupAirTriangles(g);
			airGroups += HDRenderGroupAirTriangles(g) ? 1 : 0;
			airBoxed += HDRenderGroupAirBoxed(g);

			for (int j = 0; j < HDRenderGroupPairRanges(g); j++)
			{
				HDPairRange piece;

				if (HDRenderGroupPairRange(g, j, &piece) && piece.face < 0 &&
					!piece.inner)
					here -= piece.triangles;
			}

			intruding += here;
			intrudingGroups += here ? 1 : 0;
		}

		printf("intrusion: overlap pairs=%d intruding tris=%d in %d groups "
			"holder=inside cull=opaque faces=%d bound=%d unbound=%d "
			"band_unclaimed=%d air_ms=%d air_tris=%d air_groups=%d "
			"air_pairs=%d air_boxed=%d air=%d covered=%d\n",
			CountIntrusionPairs(), intruding, intrudingGroups,
			(int)claimed.size(), bound, unbound, band,
			(int)(g_air_ms + HDRenderAirMicroseconds() / 1000.0 + 0.5),
			airTris, airGroups, airPairs, airBoxed, g_air_option,
			HDRenderCoveredFaces());

		// The engine's HDLOG carry lines (ReportRoomCarry), from the build.
		{
			int carryTris = 0;

			for (int i = 0; i < HDRenderCarryPairs(); i++)
			{
				int group = -1, host = -1, n = 0;

				if (!HDRenderCarryPair(i, &group, &host, &n))
					continue;

				carryTris += n;
				printf("carry: host=%d group=%d tris=%d\n", host, group, n);
			}

			printf("carry: pairs=%d tris=%d air=%d\n", HDRenderCarryPairs(),
				carryTris, g_air_option);
		}

		// And its HDLOG hull lines (ReportRoomIntrusions), with -air 1 or 2.
		for (int g = 0; g_air_option && g < HDRenderGroupCount(); g++)
		{
			int air = 0, covered = 0;
			const bool hull = HDRenderGroupHull(g, &air, &covered);

			if (air)
				printf("hull: group=%d air_tris=%d host_art=%d hull=%d "
					"behind=%d\n", g, air, covered, hull ? 1 : 0,
					HDRenderGroupHullBehind(g));
		}

		// And its HDLOG covered lines (ReportRoomIntrusions), the same.
		for (int i = 0; g_air_option; i++)
		{
			int face = 0, group = 0, holder = 0, behind = 0, airs = 0;
			float area = 0.0f, own[3], held[3];
			float centre[3], normal[3];

			if (!HDRenderCoveredFace(i, &face, &group, &holder, &area, own,
				held, &airs, centre, normal, &behind))
				break;

			const float to = area > 0.0f ? 100.0f / area : 0.0f;

			printf("covered: face=%d group=%d holder=%d area=%d "
				"own=%d/%d/%d held=%d/%d/%d airs=%d centre=%d,%d,%d "
				"normal=%d,%d,%d behind=%d\n", face, group,
				holder, (int)(area + 0.5f), (int)(own[0] * to + 0.5f),
				(int)(own[1] * to + 0.5f), (int)(own[2] * to + 0.5f),
				(int)(held[0] * to + 0.5f), (int)(held[1] * to + 0.5f),
				(int)(held[2] * to + 0.5f), airs, (int)floorf(centre[0] + 0.5f),
				(int)floorf(centre[1] + 0.5f), (int)floorf(centre[2] + 0.5f),
				(int)floorf(normal[0] * 1000.0f + 0.5f),
				(int)floorf(normal[1] * 1000.0f + 0.5f),
				(int)floorf(normal[2] * 1000.0f + 0.5f), behind);
		}

		for (size_t i = 0; i < g_intrusions_built.size(); i++)
		{
			const int hits = HDRenderIntrusionHits((int)i);

			if (hits)
				printf("  overlap: room %3d holds the art of room %3d - "
					"%d triangles, no portal between them faces=%d bound=%d\n",
					g_intrusions_built[i].holder,
					g_intrusions_built[i].group, hits, pairFaces[i],
					pairBound[i]);
		}

		// The engine's HDLOG unlinked lines, word for word but the prefix.
		for (int i = 0; i < airPairs; i++)
		{
			int group = 0, host = 0, tris = 0;

			if (HDRenderAirPair(i, &group, &host, &tris))
				printf("unlinked level=%s group=%d host=%d tris=%d\n", stem,
					group, host, tris);
		}

		printf("unlinked: %d triangle(s) of %d group(s) taken out of the "
			"open half as pieces of their own\n", openAir,
			(int)openGroups.size());
	}
	/* What the openings really took away, per box and per room, out of the
	 * engine's own build. A box that caught nothing is printed too - a
	 * doorway the remaster's art never covered and a box in the wrong place
	 * look exactly alike in a picture, and only this number tells them
	 * apart. */
	if (!g_cuts_built.empty())
	{
		printf("Z: %d cut box(es), %d triangles dropped in %d groups\n",
			HDMarkersCutCount(), stats.cutTris, stats.cutGroups);

		for (int b = 0; b < HDMarkersCutCount(); b++)
		{
			int tris = 0, groups = 0;

			for (size_t i = 0; i < g_cuts_built.size(); i++)
			{
				if (g_cuts_built[i].box != b)
					continue;

				const int hits = HDRenderCutHits((int)i);

				if (!hits)
					continue;

				tris += hits;
				groups++;
				printf("  cut %d: room %3d loses %d triangles\n",
					b, g_cuts_built[i].group, hits);
			}

			if (!tris)
				printf("  cut %d: NOTHING - the remaster drew no triangle in "
					"that box\n", b);
			else
				printf("  cut %d: %d triangles out of %d rooms\n",
					b, tris, groups);
		}
	}
	printf("limits: widest draw call reaches over %d vertices, widest single "
		"triangle %d (a 16 bit index reaches 65536), device holds textures "
		"up to %d\n",
		stats.indexWindow, stats.triangleWindow, stats.maxTextureSide);
	printf("motion: %d scrolling part(s) in %d draw calls (%d triangles), "
		"%d vertices with a wind weight\n",
		stats.scrollParts, stats.scrollRanges, stats.scrollTris,
		stats.swayVertices);
	// Material 13 is the flipbook: four pages, one per frame time, the
	// part's own page first. `kept` is the parts whose numbers did not
	// resolve and that stand still.
	printf("flipbook: %d part(s), %d of them running in %d draw call(s), "
		"%d kept their own page\n",
		stats.flipbookParts, stats.flipbookDrawn, stats.flipbookRanges,
		stats.flipbookDropped);
	/* Material 15 is the book: N consecutive pages from the part's own, one
	 * per step, the part's phase first. The totals the engine's motion line
	 * carries, then one line per part and pass with the same fields as the
	 * engine's `HDLOG book` line. */
	printf("book: %d part(s) turning, %d of them additive, %d kept their own "
		"page, %d distinct page(s), %d draw call(s), step %.3f..%.3f s\n",
		stats.bookParts, stats.bookAdditive, stats.bookDropped,
		stats.bookPages, stats.bookRanges, stats.bookStepMin,
		stats.bookStepMax);

	for (int i = 0; i < HDRenderBookPartCount(); i++)
	{
		HDRenderBookPart book;

		if (HDRenderBookPartInfo(i, &book))
			printf("  book group=%d part=%d page=%d..%d frames=%d step=%.3f "
				"phase=%d pass=%d weight=%.3f dropped=%d\n", book.group,
				book.part, book.page0, book.pageLast, book.pages, book.step,
				book.phase, book.pass, book.weight, book.dropped);
	}
	// And the water side: how many HD vertices carry a surface mark, and how
	// many of those the corner rule never found and the plane flood answered
	// for.
	printf("water: %d mark(s) stamped, %d on the surface, %d on the whole "
		"room, %d of them flooded, %d plane(s) dropped\n",
		stats.markedVertices, stats.markedShimmer, stats.markedRoomShimmer,
		stats.markedFlooded, stats.waterPlanesDropped);
	/* And how many of the marked vertices are not a flat surface at all -
	 * a wall or a pillar standing at the water's own height. The geometry,
	 * the UV wobble and the caustic leave those alone; the colour term
	 * still has them, as the classic path does. */
	printf("waterline: %d marked vertex/vertices whose normal is not vertical "
		"(the wall at the waterline)\n", stats.markedWall);
	// And the gloss: the opaque parts of materials 3 and 17 whose own
	// numbers gave them one.
	printf("gloss: %d glossy part(s) (materials 3 and 17)\n",
		stats.glossParts);

	/* And the sheen: what the four sources named (the engine's GLOSSLOG
	 * lines), the lamps and the light field, the draw calls that take the
	 * gloss variant and the ones that keep the plain one. Without -roombox
	 * there is no floor square and no room box, so no seed and no field -
	 * only the material, the table and the file. */
	{
		HDRenderGlossLevel gloss;

		HDRenderGlossLevelInfo(&gloss);
		printf("sheen: %d page(s) in %d draw call(s)%s; parts mat=%d seed=%d "
			"table=%d file=%d; table %d line(s), file %d page(s) %d bad; "
			"lamps %d, cubes %d\n", gloss.pages, gloss.draws,
			g_gloss_max ? " (-glossmax)" : "", gloss.partsMat, gloss.partsSeed,
			gloss.partsTable, gloss.partsFile, gloss.tableLines, gloss.filePages,
			gloss.fileBad, gloss.lamps, gloss.cubes);
		printf("sheen field: %d x %d x %d cells, %d lit, %d weak, %d KiB, %.1f ms\n",
			gloss.fieldDims[0], gloss.fieldDims[1], gloss.fieldDims[2], gloss.fieldLit,
			gloss.fieldWeak, gloss.fieldBytes / 1024, gloss.fieldMs);
		printf("sheen skipped: ripple=%d caustic=%d flat=%d scroll=%d flipbook=%d "
			"book=%d clear=%d\n", gloss.skipped.ripple, gloss.skipped.caustic,
			gloss.skipped.flat, gloss.skipped.scroll, gloss.skipped.flipbook,
			gloss.skipped.book, gloss.skipped.clear);

		for (int i = 0; i < HDRenderGlossPageCount(); i++)
		{
			HDRenderGlossPage p;

			if (HDRenderGlossPageInfo(i, &p))
				printf("  sheen page %5d %-6s %-5s strength %.3f power %.0f F0 %.3f "
					"tris %d\n", p.page, HDGlossClassName(p.cls),
					HDGlossSourceName(p.source), p.strength, p.power, p.f0, p.tris);
		}

		/* The rooms with a clear floor (HD_PASS_CLEAR) - RICH2's lobby and
		 * its mirrored copy, RICHCUT2's and TITLE's floors - and how many
		 * glossy draw calls they carry: their own pages shine where a source
		 * names them, like every other room's, and their clear floors and
		 * the lobby's pages do not. */
		int clear_rooms = 0, clear_sheen = 0;

		for (int g = 0; g < HDRenderGroupCount(); g++)
			if (HDRenderGroupHasClear(g))
			{
				clear_rooms++;
				clear_sheen += HDRenderGroupSheenRanges(g);

				if (HDRenderGroupSheenRanges(g))
					printf("  sheen in clear-floor room %d: %d draw call(s)\n", g,
						HDRenderGroupSheenRanges(g));
			}

		printf("sheen: %d room(s) with a clear floor, %d glossy draw call(s) in them\n",
			clear_rooms, clear_sheen);
	}

	/* The destructible pieces, one line each, with the room they stand in
	 * and their box - the numbers hd_level.cpp pairs against the classic
	 * MESH_INFO list. -shatter then hides the drawn room's. */
	for (int i = 0; i < g_geom_count; i++)
	{
		const float* b = g_geom_box[i];
		const float mins[3] = { b[0], b[1], b[2] };
		const float maxs[3] = { b[3], b[4], b[5] };

		printf("destructible: group %d, box %.0f..%.0f %.0f..%.0f %.0f..%.0f - geometry: %s\n",
			room, mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2],
			HDRenderGroupHasGeometry(room, mins, maxs) ? "YES (baked, stays suppressed)" : "NO (drawn the classic way)");
	}

	/* The horizontal planes of the drawn group, and the answer the
	 * water-surface gate gives for a plane one names. A NO is a plane the
	 * remaster left empty - there the classic water face is drawn on top of
	 * the HD room (hd_level.cpp, BuildWaterSurfaces). */
	printf("planes: %d horizontal plane(s) of art in group %d (%d in the level)\n",
		HDRenderGroupFlatPlanes(room), room, stats.flatPlanes);
	printf("alpha: %d see-through part(s) in the alpha pass (material 14), "
		"%d draw call(s)\n", stats.blendParts, stats.blendRanges);
	printf("clear: %d floor part(s) on %d part-way clear page(s), %d draw "
		"call(s)\n", stats.clearParts, stats.clearPages, stats.clearRanges);

	/* The mirrors in the rooms' walls the build found - none without
	 * -roombox, which carries the rooms' origins the rule needs. */
	printf("mirrors: %d\n", HDRenderMirrorCount());

	for (int i = 0; i < HDRenderMirrorCount(); i++)
	{
		HDMirror m;

		if (!HDRenderMirrorInfo(i, &m))
			continue;

		printf("  mirror: room %d, plane %c = %.0f, copy on side %d, pane %d, "
			"%d behind, %d of them reflected, copy box %.0f..%.0f, "
			"%.0f..%.0f, %.0f..%.0f\n", m.group, m.axis == 0 ? 'x' : 'z',
			m.plane, m.side, m.pane, m.behind, m.mirrored, m.mins[0],
			m.maxs[0], m.mins[1], m.maxs[1], m.mins[2], m.maxs[2]);
	}

	// The still water surfaces the .TRG builds itself - the same line the
	// engine writes to HDLOG at level build.
	if (stats.waterParts)
		printf("basin parts=%d (%d additive, %d opaque, %d still), "
			"triangles=%d, sub-rect tiles=%d, smallest UV span=%d/255, "
			"ripple capped at %.4f page units\n", stats.waterParts,
			stats.waterAdditiveParts, stats.waterOpaqueParts,
			stats.waterStillParts, stats.waterTris, stats.waterSubRectParts,
			stats.waterSmallestSpan, stats.waterRippleCap);

	for (int i = 0; i < g_flat_count; i++)
	{
		const int* b = g_flat_box[i];

		printf("planes: group %d, plane y %d, box x %d..%d z %d..%d - art: %s\n",
			room, b[0], b[1], b[3], b[2], b[4],
			HDRenderGroupFlatArt(room, b[0], b[1], b[2], b[3], b[4])
				? "YES (the remaster built it, the classic face stays out)"
				: "NO (the classic water face is drawn on top)");
	}

	if (HDRenderDestructibleCount())
	{
		printf("destructible: %d piece(s):\n",
			HDRenderDestructibleCount());

		for (int i = 0; i < HDRenderDestructibleCount(); i++)
		{
			int group = -1;
			unsigned int material = 0;
			float mins[3], maxs[3];

			if (!HDRenderDestructibleInfo(i, &group, &material, mins, maxs))
				continue;

			printf("     %2d room %3d material 0x%04x copy %d  x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f\n",
				i, group, material, (material >> 8) & 0x7F,
				mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2]);

			if (g_shatter && group == room)
				HDRenderDestructibleHide(i, true);

			for (size_t k = 0; k < g_smash_pieces.size(); k++)
				if (g_smash_pieces[k] == i)
					HDRenderDestructibleHide(i, true);
		}

		if (!g_smash.empty())
			probestatics::SmashHide(stem, g_smash);
	}
	// The wind belongs to material 8, and this says how much of the .TRG's
	// own sway weight was on other materials.
	printf("sway: %d moving part(s) (materials 1, 7, 8); %d vertices kept "
		"their weight, "
		"%d lost it\n", stats.swayParts, stats.swayVertices, stats.swayDropped);
	// The opaque parts that draw with the alpha test on, which is the three
	// cut-out materials 6, 7 and 8 and nothing else.
	printf("cut-out: %d opaque part(s) (materials 6, 7, 8)\n",
		stats.cutoutParts);
	// The additive parts whose .TRG material handed them a weight of their
	// own, and the ones that also swing on the game clock.
	printf("additive: %d part(s) weighted (smallest %.3f), %d of them "
		"swinging\n", stats.addWeightParts, stats.addWeightMin,
		stats.addSwingParts);
	// The material 2 parts the swing fence stills, and the stacked seas
	// drawn once.
	printf("additive: %d material 2 part(s) still (swing under half a "
		"second); %d stacked sea(s) drawn once, %d copied triangles left out\n",
		stats.addStillParts, stats.stackedParts, stats.stackedTris);
	/* The glow shader's parts: whether their card sides fade, which normal
	 * their |N.V| takes, and the asset they belong to with its page-v axis,
	 * its ends in world units and the end fade's length. */
	printf("glow: %d material 2 part(s) in %d asset(s), %d with an edge fade, "
		"%d on the face normal\n", stats.glowParts, stats.glowAssets,
		stats.glowEdgeParts, stats.glowFaceParts);

	for (int i = 0; i < HDRenderGlowPartCount(); i++)
	{
		HDRenderGlowPart g;

		if (!HDRenderGlowPartInfo(i, &g))
			continue;

		printf("glow part: group %d part %d page %d u %d..%d v %d..%d edge %d "
			"normal %s (%d of %d corners disagree) asset %d axis %.3f %.3f %.3f "
			"ends %.0f..%.0f fade %.0f\n", g.group, g.part, g.page, g.u0, g.u1,
			g.v0, g.v1, g.edge, g.face ? "face" : "file", g.disagree, g.corners,
			g.asset,
			g.axis[0], g.axis[1], g.axis[2], g.lo, g.hi, g.fade);
	}

	printf("read %.0f ms, build %.0f ms phases=", stats.readMs, stats.buildMs);

	for (int p = 0; p < HD_BEGIN_PHASES; p++)
		printf("%s%s:%.1f", p ? "," : "", HDRenderBeginPhaseName(p),
			stats.phaseMs[p]);

	printf(" hash=%016llx hash_ms=%.0f\n", stats.hash, stats.hashMs);

	printf("\n-- the pages ------------------------------------------------\n");
	printf("%d named, %d distinct: %d from 5, %d from 4, %d white, %d failed\n",
		stats.pages.pages, stats.pages.slots, stats.pages.fromGame5,
		stats.pages.fromGame4, stats.pages.fromWhite,
		stats.pages.failed);
	printf("%d mip levels, widest %d, %d past 1024, %.1f MiB of %s "
		"on the device\n", stats.pages.mipLevels, stats.pages.widest,
		stats.pages.bigPages, stats.pages.bytes / 1048576.0,
		stats.pages.formatUsed == HD_PAGE_FORMAT_DXT5 ? "DXT5" : "A8R8G8B8");
	// The pages whose alpha channel really cuts. Those, and only those,
	// make the opaque pass run its alpha test.
	printf("%d of them are cut-out pages (alpha below 128 on at least 2%% "
		"of the top mip)\n", stats.pages.cutoutPages);

	/* Which ones, by page number, and how much of each is hole. Without the
	 * list the count alone cannot be held against the .TRG's own materials,
	 * and a page in this list that no leaf ever touches is a real
	 * regression, not a rounding difference. */
	if (stats.pages.cutoutPages > 0)
	{
		printf("   page(hole %%):");

		for (int s = 0; s < HDPageCacheSlots(); s++)
			if (HDPageCacheSlotCutout(s))
				printf(" %d(%d%%)", HDPageCachePageOfSlot(s),
					HDPageCacheSlotCutoutPermille(s) / 10);

		printf("\n");
	}
	printf("search %.0f ms, decode %.0f ms (%d threads), upload %.0f ms, total %.0f ms\n",
		stats.pages.readMs, stats.pages.decodeMs, stats.pages.threads,
		stats.pages.uploadMs, stats.pages.totalMs);
	printf("pool %s: commit %.1f -> %.1f MiB (+%.1f), peak %.1f MiB, "
		"texture memory %.1f -> %.1f MiB (-%.1f)\n",
		managed ? "MANAGED" : "DEFAULT",
		commitBefore, commitAfter, commitAfter - commitBefore, PeakCommitMiB(),
		(double)vramBefore / 1048576.0, (double)vramAfter / 1048576.0,
		((double)vramBefore - (double)vramAfter) / 1048576.0);

	printf("budget %s: need %.1f MiB, already there %.1f, got %.1f - "
		"%d page(s) gave up %d level(s), %d shrunk by the device, format %s, "
		"out of memory %d\n",
		stats.pages.budgetBytes > 0 ? "in force" : "none",
		stats.pages.needBytes / 1048576.0, stats.pages.haveBytes / 1048576.0,
		stats.pages.bytes / 1048576.0, stats.pages.droppedMip0,
		stats.pages.droppedLevels, stats.pages.clampedByCaps,
		stats.pages.formatUsed == HD_PAGE_FORMAT_DXT5 ? "DXT5" : "A8R8G8B8",
		stats.pages.outOfMemory);

	if (stats.pages.compressMs > 0)
		printf("compress %.0f ms (over the same threads as the decode)\n",
			stats.pages.compressMs);

	for (int i = 0; i < HDPageCacheNoteCount(); i++)
		printf("  note: %s\n", HDPageCacheNote(i));

	if (psnr)
		PrintPsnr(root, 16);

	/* The water survey.
	 *
	 * The offline twin of hd_level.cpp's BuildWaterSurfaces: for every
	 * reflecting room of the classic level, the faces that lie in the room's
	 * own minfloor plane - the plane the classic shimmer branch opens on -
	 * and, per face, the answer of HDRenderGroupFlatArt, which is the very
	 * function the engine gates on. What it cannot do is the shimmer-vertex
	 * predicate, which needs the engine's roomlets; so this is the upper
	 * bound of what the engine will restore, and the decision per room is the
	 * same majority the engine takes.
	 *
	 * The point of it is that the engine's own gate can be run over all
	 * fifteen levels without starting the game. */
	/* The water sheets (-hdwater): the classic level's floor squares,
	 * handed to the renderer exactly as hd_level.cpp's BuildWaterSheets
	 * hands the engine's - the same HDRenderSetWaterSheets finds the water,
	 * joins the bodies and classes them - and the WATERLOG lines it writes.
	 * The tool's room y origin is 0, the classic file's own. */
	if (g_hdwater >= 0 && g_roombox.empty())
		printf("-hdwater: needs -roombox\n");
	else if (g_hdwater >= 0)
	{
		Tr5Level level;
		std::string error;
		const std::string trc = g_roombox + "\\" + stem + ".TRC";
		std::wstring wide;

		for (size_t i = 0; i < trc.size(); i++)
			wide.push_back((wchar_t)(unsigned char)trc[i]);

		if (!level.load(wide, error))
			printf("-hdwater: classic level not loadable: %s\n", error.c_str());
		else
		{
			const int nr = level.roomCount();
			std::vector<HDWaterRoom> rooms((size_t)nr);
			std::vector<std::vector<signed char> > floors((size_t)nr);
			std::vector<std::vector<short> > below((size_t)nr);
			std::vector<std::vector<signed char> > ceilings((size_t)nr);
			std::vector<std::vector<unsigned short> > slopes((size_t)nr);
			std::vector<std::vector<short> > sinks((size_t)nr);

			if (room >= 0 && room < nr)
			{
				g_seed_origin[0] = (float)level.rooms()[(size_t)room].originX;
				g_seed_origin[1] = (float)level.rooms()[(size_t)room].originZ;
			}

			// -lifetank game: the level's FISHTANK items (slot 413).
			for (int t = 0; g_life_tank_game && t < level.itemCount(); t++)
			{
				Tr5Item it;

				if (level.getItem(t, it) && it.objectNumber == 413)
				{
					g_life_tank_items.push_back(it.x);
					g_life_tank_items.push_back(it.z);
					g_life_tank_items.push_back((int)(unsigned short)it.flags);
				}
			}

			for (int r = 0; r < nr; r++)
			{
				const Tr5Room& rm = level.rooms()[(size_t)r];
				std::vector<Tr5Level::SectorInfo> sec;
				HDWaterRoom& w = rooms[(size_t)r];

				w.originX = rm.originX;
				w.originZ = rm.originZ;
				w.flags = (int)rm.flags;
				w.flip = rm.altRoom;
				// The engine's test for a room drawn HD (StandGroupHD; the
				// randomizer's markers with -hdm).
				w.drawn = r < HDRenderGroupCount() && StandGroupHD(r) ? 1 : 0;

				if (!level.sectorInfo(r, sec, error))
					continue;

				w.sizeX = rm.numXSectors;
				w.sizeZ = rm.numZSectors;

				if (g_life)
				{
					g_life_rooms.resize((size_t)nr);

					LifeRoom& lr = g_life_rooms[(size_t)r];

					lr.x = rm.originX;
					lr.z = rm.originZ;
					lr.sizeX = w.sizeX;
					lr.sizeZ = w.sizeZ;
					lr.ceiling.assign(sec.size(), -127);
					lr.above.assign(sec.size(), -1);
				}
				floors[(size_t)r].assign(sec.size(), (signed char)-127);
				below[(size_t)r].assign(sec.size(), (short)-1);
				ceilings[(size_t)r].assign(sec.size(), (signed char)-127);
				slopes[(size_t)r].assign(sec.size() * 4, (unsigned short)0);
				sinks[(size_t)r].assign(sec.size(), (short)-1);

				for (size_t i = 0; i < sec.size(); i++)
				{
					const size_t at = (size_t)(sec[i].sx * w.sizeZ + sec[i].sz);

					if (at >= sec.size())
						continue;

					if (g_life)
					{
						LifeRoom& lr = g_life_rooms[(size_t)r];

						lr.ceiling[at] = sec[i].ceiling;
						lr.above[at] = sec[i].roomAbove >= 255 ? -1 :
							sec[i].roomAbove;
					}

					floors[(size_t)r][at] = (signed char)sec[i].floor;
					below[(size_t)r][at] = (short)(sec[i].roomBelow >= 255 ||
						sec[i].roomBelow < 0 ? -1 : sec[i].roomBelow);
					ceilings[(size_t)r][at] = (signed char)sec[i].ceiling;
					ProbeWaterWords(level, r, sec[i].sx, sec[i].sz,
						&slopes[(size_t)r][at * 4], &sinks[(size_t)r][at]);
				}

				if (!sec.empty())
				{
					w.floor = &floors[(size_t)r][0];
					w.below = &below[(size_t)r][0];
					w.ceiling = &ceilings[(size_t)r][0];
					w.slope = &slopes[(size_t)r][0];
					w.sink = &sinks[(size_t)r][0];
				}
			}

			// The sinks: the camera table's places and strengths.
			std::vector<Tr5Level::CameraRecord> cams;
			std::vector<HDWaterSink> table;
			std::string camError;

			if (level.cameraRecords(cams, camError))
				for (size_t c = 0; c < cams.size(); c++)
				{
					HDWaterSink k;

					k.x = cams[c].x;
					k.y = cams[c].y;
					k.z = cams[c].z;
					k.strength = cams[c].room;
					table.push_back(k);
				}

			HDRenderWaterSinks(table.empty() ? 0 : &table[0],
				(int)table.size());
			ProbeWaterCorners(level, rooms);

			const int section = !_strnicmp(stem, "ANDREA", 6) ? HD_WATER_ROME :
				!_strnicmp(stem, "JOBY", 4) ? HD_WATER_RUSSIA :
				!_strnicmp(stem, "ANDY", 4) ? HD_WATER_IRELAND :
				!_strnicmp(stem, "RICH", 4) ? HD_WATER_VCI : HD_WATER_ROME;
			char line[512];

			HDRenderWaterFile(g_water_file.empty() ? 0 : g_water_file.c_str());
			ProbeWaterFilms(level);
			HDRenderSetWaterSheets(nr ? &rooms[0] : 0, nr, section, stem,
				g_hdwater);
			ProbeWetSlopes(rooms);

			/* -surfaceroom and -surfacealways: the game's rule over the
			 * same rooms - the water rooms' highest square ceilings and each
			 * group's highest art (group r is room r at the load) - with the
			 * deep look's own answer, and the next water room's art for the
			 * margin. */
			if (g_surface_room || g_surface_always)
			{
				HDRenderWaterLevel wl;
				std::vector<int> wet((size_t)nr, 0), art((size_t)nr, 0);
				std::vector<float> ceilTop((size_t)nr, 1.0e9f);
				std::vector<float> artTop((size_t)nr, 0.0f);
				float top = 1.0e9f;

				HDRenderWaterLevelInfo(&wl);

				for (int r = 0; r < nr; r++)
				{
					const HDWaterRoom& w = rooms[(size_t)r];
					float mins[3], maxs[3];

					wet[(size_t)r] =
						(w.flags & HD_WATER_ROOM_UNDERWATER) ? 1 : 0;

					for (int i = 0; w.floor && w.ceiling &&
						i < w.sizeX * w.sizeZ; i++)
						if (w.floor[i] != -127 && w.ceiling[i] != -127 &&
							w.ceiling[i] * 256.0f < ceilTop[(size_t)r])
							ceilTop[(size_t)r] = w.ceiling[i] * 256.0f;

					if (w.drawn && HDRenderGroupBounds(r, mins, maxs))
					{
						art[(size_t)r] = 1;
						artTop[(size_t)r] = mins[1] + (float)w.originY;
					}

					if (wet[(size_t)r] && ceilTop[(size_t)r] < top)
						top = ceilTop[(size_t)r];
				}

				const int s = nr ? HDWaterSurfaceRoom(&wet[0], &ceilTop[0],
					&art[0], &artTop[0], nr, kHDWaterSurfaceReach) : -1;
				int next = -1;

				for (int r = 0; r < nr; r++)
					if (r != s && wet[(size_t)r] && art[(size_t)r] &&
						(next < 0 || artTop[(size_t)r] < artTop[(size_t)next]))
						next = r;

				bool drawn = s == room;

				for (int i = 0; i < g_with_count && !drawn; i++)
					drawn = g_with_group[i] == s;

				const bool add = g_surface_always && wl.underDeep && s >= 0 &&
					!drawn && g_with_count < kWithMax;

				if (add)
				{
					g_with_group[g_with_count] = s;
					g_with_offset[g_with_count][0] = (float)(rooms[(size_t)s].
						originX - rooms[(size_t)room].originX);
					g_with_offset[g_with_count][1] = 0.0f;
					g_with_offset[g_with_count][2] = (float)(rooms[(size_t)s].
						originZ - rooms[(size_t)room].originZ);
					g_with_count++;
				}

				if (g_surface_room)
					printf("surfaceroom: %s deep=%d top=%.0f room=%d "
						"art_top=%.0f next=%d next_top=%.0f used=%d "
						"added=%d\n", stem, wl.underDeep, top, s,
						s >= 0 ? artTop[(size_t)s] : 0.0f, next,
						next >= 0 ? artTop[(size_t)next] : 0.0f,
						wl.underDeep && s >= 0 ? 1 : 0, add ? 1 : 0);
			}
			ProbeCensus(rooms, root, stem);
			ProbeMarbleCensus(rooms, root, stem);
			ProbeWaterFalls(level, stem);

			// -flip: the water follows the swapped pairs, as FlipMap's hook.
			for (size_t k = 0; k < walkoff::g_flip_rooms.size(); k++)
			{
				const int a = walkoff::g_flip_rooms[k];

				if (a >= 0 && a < nr && rooms[a].flip >= 0)
					HDRenderWaterFlipRoom(a, rooms[a].flip);
			}
			ProbeCardPages(root, managed, threads);

			/* -reload: the sheets set again over the kept build, as the
			 * resident build sets them, with a ring of the first set left on
			 * the first water square - the second set starts from nothing. */
			if (g_reload_kept)
			{
				bool ringed = false;

				for (int r = 0; r < nr && !ringed; r++)
					for (int at = 0; rooms[r].below &&
						at < rooms[r].sizeX * rooms[r].sizeZ && !ringed; at++)
					{
						const int pit = rooms[r].below[at];

						if (pit < 0 || pit >= nr ||
							!(rooms[pit].flags & HD_WATER_ROOM_UNDERWATER))
							continue;

						HDRenderWaterRing((float)(rooms[r].originX +
							(at / rooms[r].sizeZ) * 1024 + 512),
							(float)(rooms[r].originZ + (at % rooms[r].sizeZ) *
							1024 + 512), 0.5f, 120.0f);
						ringed = true;
					}

				LARGE_INTEGER a, b, rate;

				QueryPerformanceCounter(&a);
				ProbeWaterFilms(level);
				HDRenderSetWaterSheets(nr ? &rooms[0] : 0, nr, section, stem,
					g_hdwater);
				ProbeWaterFalls(level, stem);
				ProbeCardPages(root, managed, threads);
				QueryPerformanceCounter(&b);
				QueryPerformanceFrequency(&rate);
				printf("-reload: the sheets set again over the kept build%s "
					"ms=%.1f\n", ringed ? " (a ring of the first set on them "
					"before)" : "", (double)(b.QuadPart - a.QuadPart) * 1000.0 /
					(double)rate.QuadPart);
			}

			for (size_t i = 0; i + 4 < g_feet.size(); i += 5)
				HDRenderWaterFootAdd(g_feet[i], g_feet[i + 1], g_feet[i + 2],
					g_feet[i + 3], g_feet[i + 4]);

			for (size_t i = 0; i < g_ripples.size(); i++)
				HDRenderWaterRing(g_ripples[i].x, g_ripples[i].z,
					g_ripples[i].age, g_ripples[i].size);

			HDRenderWaterLara(g_lara_set ? 1 : 0, g_lara_x, g_lara_z);
			HDRenderWaterTiming(1, 0, 0);
			HDRenderWaterLogLine(-1, stem, g_hdwater, line, sizeof(line));
			printf("%s\n", line);

			for (int b = 0; b < HDRenderWaterBodies(); b++)
				if (HDRenderWaterLogLine(b, stem, g_hdwater, line,
					sizeof(line)))
					printf("%s\n", line);

			for (int f = 0; HDRenderWaterFallLine(f, stem, line,
				sizeof(line)); f++)
				printf("%s\n", line);

			for (int f = 0; HDRenderWaterFootLine(f, stem, line,
				sizeof(line)); f++)
				printf("%s\n", line);

			for (int s = 0; HDRenderWaterSegLine(s, stem, line,
				sizeof(line)); s++)
				printf("%s\n", line);

			for (int a = 0; HDRenderWaterArtLine(a, stem, line,
				sizeof(line)); a++)
				printf("%s\n", line);

			for (int w = 0; HDRenderWaterWetLine(w, stem, line,
				sizeof(line)); w++)
			{
				printf("%s\n", line);
				ProbeWetMap(w);
				ProbeWetArt(w, root, stem);
			}

			for (const std::string& m : g_mist_lines)
				printf("%s\n", m.c_str());

			/* The springs, after the sheets and before the life, which reads
			 * them as its bodies' springs - the engine's order. */
			if (g_hdwater == 1)
				SpringBuild(root, stem, room);

			// -wetlara: her outfit on the card.
			if (g_wet_lara)
				WetLaraBuild(root, stem);

			// -life: the water's life on the bodies just built.
			if (g_life && g_hdwater)
				LifeBuild(root, stem);

			// -rippleat: whether a ripple there loses its classic sprite.
			for (size_t r = 0; r + 2 < g_ripple_at.size(); r += 3)
				printf("rippleat %d,%d,%d: %s\n", g_ripple_at[r],
					g_ripple_at[r + 1], g_ripple_at[r + 2],
					HDRenderWaterSheetUnder(g_ripple_at[r], g_ripple_at[r + 1],
					g_ripple_at[r + 2]) ? "a sheet draws it as a ring - no "
					"sprite" : "no sheet within a click - the classic sprite");
		}
	}

	if (!g_survey.empty())
	{
		Tr5Level level;
		std::string error;
		const std::string trc = g_survey + "\\" + stem + ".TRC";
		std::wstring wide;

		for (size_t i = 0; i < trc.size(); i++)
			wide.push_back((wchar_t)(unsigned char)trc[i]);

		printf("\n-- the water survey -----------------------------------------\n");

		if (!level.load(wide, error))
			printf("classic level not loadable: %s\n", error.c_str());
		else
		{
			int rooms = 0, faces = 0, covered = 0, restored = 0, restoredRooms = 0;
			// Rooms handed to the .TRG because it builds the water itself.
			int sheetRooms = 0, sheetFaces = 0;
			// The texinfos the restored faces name - the way into the
			// classic animation ranges.
			std::vector<int> waterTiles;

			for (int r = 0; r < level.roomCount() && r < HDRenderGroupCount(); r++)
			{
				const Tr5Room& rm = level.rooms()[(size_t)r];

				// ROOM_MIST 0x100 or ROOM_REFLECT 0x200 - the two flags whose
				// branch ever sets the surface bit (drawroom.cpp).
				if (!(rm.flags & 0x300))
					continue;

				std::vector<Tr5Level::FaceInfo> flat;

				if (!level.roomFaces(r, flat, error))
					continue;

				/* `inPlane` every classic water face of the room, `hit` how
				 * many of them the .TRG has any flat art for (the majority,
				 * counted over all of them), `sheet` how many carry the
				 * .TRG's own water and are therefore not drawn, `here` what
				 * is left to restore. The engine's own split,
				 * BuildWaterSurfaces in hd_level.cpp. */
				int inPlane = 0, here = 0, hit = 0, sheet = 0;
				std::vector<int> here_tiles;

				for (size_t f = 0; f < flat.size(); f++)
				{
					const int corners = flat[f].quad ? 4 : 3;
					bool level_face = true;

					for (int c = 1; c < corners; c++)
						if (flat[f].v[c][1] != flat[f].v[0][1])
							level_face = false;

					if (!level_face || (int)flat[f].v[0][1] != rm.yBottom)
						continue;

					int x0 = 1 << 30, x1 = -(1 << 30), z0 = 1 << 30, z1 = -(1 << 30);

					for (int c = 0; c < corners; c++)
					{
						const int x = (int)flat[f].v[c][0] - rm.originX;
						const int z = (int)flat[f].v[c][2] - rm.originZ;

						if (x < x0) x0 = x;
						if (x > x1) x1 = x;
						if (z < z0) z0 = z;
						if (z > z1) z1 = z;
					}

					inPlane++;

					if (HDRenderGroupFlatArt(r, rm.yBottom, x0, z0, x1, z1))
						hit++;

					/* Material 16 here means the remaster lays its own water
					 * over this piece of ground, so the classic face is not
					 * drawn. It stays in `hit`/`inPlane` above, because the
					 * majority is counted over the whole plane. */
					if (HDRenderGroupWaterArt(r, rm.yBottom, x0, z0, x1, z1))
					{
						sheet++;
						continue;
					}

					here++;
					here_tiles.push_back(flat[f].texInfo);
				}

				if (!inPlane)
					continue;

				rooms++;
				faces += inPlane;
				covered += hit;

				const bool restore = here && hit * 2 < inPlane;

				if (restore)
				{
					restored += here;
					restoredRooms++;

					// Only a face that is really drawn asks for a frame.
					for (size_t t = 0; t < here_tiles.size(); t++)
					{
						bool seen = false;

						for (size_t w = 0; w < waterTiles.size(); w++)
							if (waterTiles[w] == here_tiles[t])
								seen = true;

						if (!seen)
							waterTiles.push_back(here_tiles[t]);
					}
				}

				if (sheet)
				{
					sheetRooms++;
					sheetFaces += sheet;
				}

				char verdict[96];

				if (restore)
					sprintf(verdict, "RESTORE %d of %d", here, inPlane);
				else
					sprintf(verdict, "leave it to the .TRG");

				printf("  room %3d  y %6d  %3d classic face(s), the .TRG covers "
					"%3d, of them water sheet %3d  ->  %s\n", r, rm.yBottom,
					inPlane, hit, sheet, verdict);
			}

			printf("  %s: %d reflecting room(s), %d face(s) in the minfloor "
				"plane, %d covered by the .TRG, %d face(s) in %d room(s) "
				"restored; %d room(s) with %d face(s) carry the .TRG\'s own "
				"water sheet\n", stem, rooms, faces, covered, restored,
				restoredRooms, sheetRooms, sheetFaces);
			printf("  (the majority is taken over every face in the "
				"plane; a face under the .TRG's own sheet is then dropped "
				"from what a restored room draws)\n");


			/* And which HD pages the restored faces' own animation frames
			 * have. The offline twin of the engine's own HDLOG line - the
			 * classic level's animated texture ranges, the tiles in them,
			 * the 64x64 cell each tile sits in and the .DDS the .TEX names
			 * for that cell. */
			WaterFrames(level, root, stem, waterTiles);
		}

		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	if (groupsOnly)
	{
		printf("\n-- the groups -----------------------------------------------\n");

		for (int g = 0; g < HDRenderGroupCount(); g++)
		{
			float mins[3], maxs[3];

			if (!HDRenderGroupBounds(g, mins, maxs))
				continue;

			printf("  group %3d  tris %6d/%5d/%5d  ranges %3d  "
				"x %6.0f..%-6.0f y %6.0f..%-6.0f z %6.0f..%-6.0f\n",
				g, HDRenderGroupTriangles(g, 0), HDRenderGroupTriangles(g, 1),
				HDRenderGroupTriangles(g, 2), HDRenderGroupRanges(g, -1),
				mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2]);
		}

		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	if (animTex)
	{
		/* What moves in this level, room by room.
		 *
		 * All three columns come out of the .TRG itself and none of them is
		 * a guess about the classic level:
		 *
		 *   scroll    a part whose material is 1 and whose param.xy is not
		 *             zero - a UV scroll in page units per second. It is the
		 *             HD twin of the classic UVROT branch (output.cpp), and
		 *             it is drawn.
		 *   sway      vertices whose TrgVertex::sway is not zero - the
		 *             remaster's own wind weight, 0..32767. Drawn.
		 *   flipbook  parts whose material is 13, the remaster's own four
		 *             frame page animation - the part's own page and the
		 *             three layers its param[0..2] name, one page per
		 *             param[3] seconds. Drawn; the flipbook line printed
		 *             earlier says how many of them resolved.
		 */
		printf("\n-- what moves ------------------------------------------------\n");
		printf("  %-6s %8s %8s %10s %8s %8s %9s\n",
			"room", "ranges", "scroll", "page/s", "swayV", "swayMax", "flipbook");

		int rooms = 0, scroll = 0, sway = 0, flip = 0;

		for (int g = 0; g < HDRenderGroupCount(); g++)
		{
			HDRenderGroupMotion m;

			if (!HDRenderGroupMotionInfo(g, &m))
				continue;

			if (!m.scrollRanges && !m.swayVertices && !m.flipbookParts)
				continue;

			rooms++;
			scroll += m.scrollRanges;
			sway += m.swayVertices;
			flip += m.flipbookParts;

			printf("  %-6d %8d %8d %5.2f/%-4.2f %8d %8d %9d\n",
				g, HDRenderGroupRanges(g, -1), m.scrollRanges,
				m.scrollU, m.scrollV, m.swayVertices, m.swayMax,
				m.flipbookParts);
		}

		printf("  %d room(s) move: %d scrolling draw calls (%d parts, %d triangles), "
			"%d swaying vertices, %d flipbook parts\n",
			rooms, scroll, stats.scrollParts, stats.scrollTris, sway, flip);

		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	const double persp = enginePersp > 0.0 ? enginePersp : EnginePersp(height, fov);
	PrintZProof(persp);

	if (noDraw)
	{
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	/* --- the camera --------------------------------------------------- */
	float mins[3], maxs[3];

	if (!HDRenderGroupBounds(room, mins, maxs))
	{
		printf("\nRoomProbe: group %d draws nothing - no picture to take.\n", room);
		HDRenderShutdown();
		DropDevice();
		return 5;
	}

	printf("\n-- room %d ---------------------------------------------------\n", room);
	printf("triangles %d opaque, %d alpha, %d additive; %d ranges\n",
		HDRenderGroupTriangles(room, 0), HDRenderGroupTriangles(room, 1),
		HDRenderGroupTriangles(room, 2), HDRenderGroupRanges(room, -1));
	printf("room-local box: x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f\n",
		mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2]);

	if (!haveCamera)
	{
		/* The middle of the room, looking at the longest wall. The longest
		 * wall runs along the wider of the two horizontal axes, so the view
		 * goes along the other one, from the near end of it. Eye height is a
		 * quarter of the room down from the ceiling - which is a quarter of
		 * the way from the smallest y, because y counts downwards. */
		const double ex = 0.5 * ((double)mins[0] + (double)maxs[0]);
		const double ez = 0.5 * ((double)mins[2] + (double)maxs[2]);
		const double dx = (double)maxs[0] - (double)mins[0];
		const double dz = (double)maxs[2] - (double)mins[2];

		cy = (double)mins[1] + 0.35 * ((double)maxs[1] - (double)mins[1]);

		if (dx >= dz)
		{
			// The long walls face along z: stand at the near z wall and look
			// across the room at the far one.
			cx = ex;
			cz = (double)mins[2] + 0.12 * dz;
			yaw = 0.0;
		}
		else
		{
			cx = (double)mins[0] + 0.12 * dx;
			cz = ez;
			yaw = 90.0;
		}

		pitch = 0.0;
		printf("camera (auto): %.0f %.0f %.0f  yaw %.0f  pitch %.0f\n",
			cx, cy, cz, yaw, pitch);
	}
	else
		printf("camera: %.0f %.0f %.0f  yaw %.0f  pitch %.0f\n", cx, cy, cz, yaw, pitch);

	printf("persp %.1f at %ldx%ld, fov %.0f deg\n", persp, width, height, fov);

	/* --- the matrices ------------------------------------------------- */
	float view[16];
	D3DMATRIX projection;
	double a = 0, b = 0, boo = 0;

	LookAt(cx, cy, cz, yaw, pitch, view);
	HDRenderZConstants(20.0, 20480.0, &a, &b, &boo);
	HDRenderMakeProjection(persp, (double)width * 0.5, (double)height * 0.5,
		a, b, &projection);

	// The camera -decide frame holds the classic faces against.
	memcpy(g_decide_view, view, sizeof(g_decide_view));
	g_decide_persp = persp;
	g_decide_width = width;
	g_decide_height = height;
	g_room_probe_seed = room;

	if (matrixCheck)
	{
		MatrixProof(room, view, projection, persp, (double)width, (double)height, a, boo);
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	if (parityCheck)
	{
		ParityProof(room, view, 200);
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	/* --- the picture -------------------------------------------------- */
	printf("\n-- the pictures ---------------------------------------------\n");

	Shot shot;
	char path[512];

	if (!ShotBegin(shot, width, height))
	{
		ShotEnd(shot);
		HDRenderShutdown();
		DropDevice();
		return 6;
	}

	Snapshot before, after;

	PoseAsClassicPath();
	Take(before);

	HDFrameLighting frame;
	HDRoomLighting lighting;
	float wibbleTable[32];

	for (int i = 0; i < 32; i++)
		wibbleTable[i] = (float)(2.0 * sin(i * 0.19634955));

	FillLighting(frame, lighting, (float)width * 0.5f, (float)height * 0.5f, view);
	frame.wibbleTable = wibbleTable;

	/* -underwater with sheets: the eye in the seed room under the water,
	 * as hd_level.cpp sets it (HDFrameDraw) - the room, the eye's y (the
	 * tool's world is room local and a room's y origin 0) and world up in
	 * view space, the view matrix's second row negated; the fog bulbs go. */
	if (g_underwater && HDRenderWaterUnderOn(room))
	{
		const float len = sqrtf(view[4] * view[4] + view[5] * view[5] +
			view[6] * view[6]);

		if (len > 0.0f)
		{
			frame.under = 1;
			frame.underRoom = room;
			frame.underEyeY = (float)cy;
			frame.underUp[0] = -view[4] / len;
			frame.underUp[1] = -view[5] / len;
			frame.underUp[2] = -view[6] / len;
			frame.bulbCount = 0;
			printf("-underwater: the eye under the water in room %d at y "
				"%.0f, world up in view space %.3f %.3f %.3f\n", room,
				(float)cy, frame.underUp[0], frame.underUp[1],
				frame.underUp[2]);
		}
	}

	printf("lighting: %d light(s), %d bulb(s), fog start %.0f, underwater %d, "
		"shimmer %d, wibble %d, water table row %d\n",
		g_light_count, g_bulb_count, g_fog_start, g_underwater ? 1 : 0,
		g_shimmer ? 1 : 0, (int)g_wibble, g_mesh_effect);
	printf("animation: motion %d, clock %.2f s, water wobble %d\n",
		g_motion ? 1 : 0, g_time, (g_water || g_shimmer) ? 1 : 0);

	// -stand: the frame's answers for the stand ranges, before the bracket;
	// -statics new: the statics' pieces.
	PrepareStand(room);
	probestatics::PreparePieces(stem, room);

	LARGE_INTEGER frameStart;

	QueryPerformanceCounter(&frameStart);
	g_device->BeginScene();
	HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);

	// -splash and -tank, on the clock the bracket now holds.
	ProbeCardsFeed();
	SpringRings(cx, cy, cz);

	// The constants under the water this frame took - ModelProbe -under's.
	if (frame.under)
	{
		float u[12];

		HDRenderWaterUnderNow(u);
		printf("-underwater: constants sigma %.6f %.6f %.6f s %.6f, light "
			"%.4f %.4f %.4f up %.2f, depth %.0f, air room %d\n", u[0], u[1],
			u[2], u[3], u[4], u[5], u[6], u[7], u[11],
			HDRenderWaterAirRoom(room));
	}

	PrintDepthTruth("(inside the HD frame)");
	/* The sky dome, first and camera centred - the classic path's own place
	 * for its horizon mesh. The world matrix is the translation to the
	 * camera and nothing else, which is exactly what
	 * phd_TranslateAbs(camera.pos) leaves on the engine's matrix stack. */
	bool skyDrew = false;

	if (sky)
	{
		D3DMATRIX skyWorld;

		memset(&skyWorld, 0, sizeof(skyWorld));
		skyWorld._11 = skyWorld._22 = skyWorld._33 = skyWorld._44 = 1.0f;
		skyWorld._41 = (float)cx;
		skyWorld._42 = (float)cy;
		skyWorld._43 = (float)cz;
		skyDrew = HDRenderSky(&skyWorld);
		ProbeSkyLayers(&skyWorld, stem);
	}

	/* -underlook: the sea seen from below where the game draws it, right
	 * after the sky (HDFrameSky) and before the rooms, with the frame's own
	 * block under the water. */
	if (g_under_game)
	{
		double gpu = -1.0;
		const bool drawn = skyDrew && HDRenderWaterLook(&frame, view,
			(float)persp, (float)width * 0.5f, (float)height * 0.5f,
			(float)cx + g_seed_origin[0], (float)cz + g_seed_origin[1],
			g_under_game_knobs ? g_under_game_set : 0, &gpu);
		float k[HD_LOOK_KNOBS];

		if (g_under_game_knobs)
			memcpy(k, g_under_game_set, sizeof(k));
		else
			HDWaterLookKnobs(k);

		printf("-underlook: %s, GPU %.4f ms (timestamps round the pass) at "
			"%ldx%ld, knobs %.2f %.2f %.2f %.0f %.0f %.0f %.2f %.0f %.0f "
			"%.0f %.2f\n", drawn ? "drawn" : skyDrew ? "NOT drawn (no deep "
			"look, no -hdwater 1 or no frame under the water)" :
			"NOT drawn (no sky)",
			gpu, width, height, k[0], k[1], k[2], k[3], k[4], k[5], k[6],
			k[7], k[8], k[9], k[10]);
	}

	/* The scene: one room, or a room and the neighbours -with names, drawn
	 * in the engine's own order out of the engine's own entry points -
	 * HDFrameDraw draws every room's HD_PASS_SOLID and then every room's
	 * HD_PASS_ADDITIVE, and -oldorder is the room-major alternative. A
	 * neighbour stands at its world offset; its lighting is the same block,
	 * which is right for the picture this proves (no dynamic light in it)
	 * and would not be for a light one. */
	bool drew = false;
	static D3DMATRIX withWorld[kWithMax];

	/* The portal window, if one was asked for, set with the very call the
	 * engine makes per room. It goes on after the sky, because the engine's
	 * sky is drawn in a bracket of its own and never carries one. */
	if (g_scissor)
	{
		if (!HDRenderScissorAvailable())
			printf("group M: this device reports no D3DPRASTERCAPS_SCISSORTEST "
				"- the rectangle is ignored, exactly as the engine ignores it\n");

		HDRenderScissor((int)g_scissor_box[0], (int)g_scissor_box[1],
			(int)g_scissor_box[2], (int)g_scissor_box[3]);
	}

	for (int i = 0; i < g_with_count; i++)
	{
		memset(&withWorld[i], 0, sizeof(withWorld[i]));
		withWorld[i]._11 = withWorld[i]._22 = withWorld[i]._33 =
			withWorld[i]._44 = 1.0f;
		withWorld[i]._41 = g_with_offset[i][0];
		withWorld[i]._42 = g_with_offset[i][1];
		withWorld[i]._43 = g_with_offset[i][2];
	}

	if (!skyOnly)
	{
		if (g_old_order)
		{
			// A room the sidecar names whole is left out here too - see
			// DrawOneGroup, which the other order goes through.
			if (!g_hdm || !HDMarkersRoomTouched(room))
				drew = HDRenderRoom(room, 0, &lighting);

			for (int i = 0; i < g_with_count; i++)
				if (!g_hdm || !HDMarkersRoomTouched(g_with_group[i]))
					HDRenderRoom(g_with_group[i], &withWorld[i], &lighting);
		}
		else
		{
			/* -window says which half of every group this run draws - the
			 * whole of it (the default), the inside half or the overhang. */
			drew = DrawOneGroup(room, 0, &lighting, HD_PASS_SOLID);

			for (int i = 0; i < g_with_count; i++)
				DrawOneGroup(g_with_group[i], &withWorld[i], &lighting,
					HD_PASS_SOLID);

			// And the -stand ranges drawn in their holder's place, and the
			// pieces of -statics new.
			DrawStandInHolders(&lighting, HD_PASS_SOLID);
			probestatics::DrawPieces(&lighting, HD_PASS_SOLID);

			/* The clear floors, once every group's solid art is in - the
			 * place HDFrameDraw gives them, before the models and the
			 * glass. */
			DrawOneGroup(room, 0, &lighting, HD_PASS_CLEAR);

			for (int i = 0; i < g_with_count; i++)
				DrawOneGroup(g_with_group[i], &withWorld[i], &lighting,
					HD_PASS_CLEAR);

			DrawStandInHolders(&lighting, HD_PASS_CLEAR);
			probestatics::DrawPieces(&lighting, HD_PASS_CLEAR);
			StandReceiveCount(&lighting);

			/* -creature: the stand-ins where HDFrameDraw draws the HD
			 * objects, after the rooms, with the creatures' mark. With
			 * -latemark the bracket stays empty, as the game's does when no
			 * HD object is near, and the stand-ins are drawn outside it, as
			 * the classic buckets draw a rat. */
			if (g_creature_late && !g_creatures.empty())
			{
				HDRenderWaterMarkBegin();
				HDRenderWaterMarkEnd(0);
			}

			for (size_t i = 0; i + 1 < g_creatures.size(); i += 2)
				if (!HDRenderWaterProbeCreature(g_creatures[i],
					g_creatures[i + 1], g_seed_origin[0], g_seed_origin[1],
					g_creature_mark && !g_creature_late ? 1 : 0))
					printf("-creature %.0f,%.0f: no sheet there - not drawn\n",
						g_creatures[i], g_creatures[i + 1]);

			for (size_t i = 0; i + 1 < g_waders.size(); i += 2)
				if (!HDRenderWaterProbeCreature(g_waders[i], g_waders[i + 1],
					g_seed_origin[0], g_seed_origin[1],
					g_creature_mark ? 1 : 0, 1))
					printf("-wader %.0f,%.0f: no sheet there - not drawn\n",
						g_waders[i], g_waders[i + 1]);

			for (size_t i = 0; i + 2 < g_swimmers.size(); i += 3)
				if (!HDRenderWaterProbeCreature(g_swimmers[i],
					g_swimmers[i + 1], g_seed_origin[0], g_seed_origin[1],
					g_creature_mark ? 1 : 0, 2, g_swimmers[i + 2]))
					printf("-swimmer %.0f,%.0f: no sheet there - not "
						"drawn\n", g_swimmers[i], g_swimmers[i + 1]);

			// -life: the floats and the fish, where the HD objects go.
			if (g_life)
				LifeFrame(view, &projection, frame.bulbCount, (float)cx,
					(float)cy, (float)cz);

			/* -twophase: the game's two brackets - HDFrameDraw closes the
			 * first after the objects and HDFrameDrawGlow opens the second
			 * after the classic buckets (output.cpp) - so the mark is
			 * tested across the bracket's end as the game draws it. */
			if (g_two_phase)
			{
				HDRenderScissorOff();
				HDRenderFrameEnd();
				HDRenderFrameBegin((const D3DMATRIX*)view, &projection,
					&frame);
			}

			/* The frame behind the water sheets, where HDFrameDrawGlow takes
			 * it: after everything opaque, before the glass, once, and only
			 * when a drawn group has a sheet. */
			bool sheet = HDRenderGroupHasSheet(room);

			for (int i = 0; i < g_with_count; i++)
				sheet = sheet || HDRenderGroupHasSheet(g_with_group[i]);

			/* -latemark: the stand-ins marked where HDFrameDrawGlow marks the
			 * rats, after the classic buckets and before the copy. */
			for (size_t i = 0; g_creature_late && i + 1 < g_creatures.size();
				i += 2)
				HDRenderWaterProbeCreatureLate(g_creatures[i],
					g_creatures[i + 1], g_seed_origin[0], g_seed_origin[1]);

			// The springs' cracks, where HDFrameDrawGlow draws them.
			SpringCrackDraw();

			// -wetstand: Lara's stand-ins, among the frame's objects.
			std::memcpy(g_wet_stand_proj, &projection,
				sizeof(g_wet_stand_proj));

			/* The room behind the models, before Lara or the stand-ins, as
			 * HDRenderWaterMarkBegin takes it before the game's objects. */
			if (g_hdwater == 1 && (g_wet_lara_ready || !g_wet_stands.empty()))
				ProbeWetRoomCopy();

			// -wetlara: Lara, where the stand-ins stand.
			if (g_wet_lara_ready)
			{
				WetLaraMatrices(view);
				ProbeWetLaraDraw(&projection, 0, HD_PASS_SOLID);

				/* The models' mirror called before the wet floor drew, as
				 * on a level's frames before a tank breaks: its warm draw. */
				if (!g_wet_no_models)
				{
					LARGE_INTEGER wf, w0, w1;
					IDirect3DQuery9* e = 0;

					QueryPerformanceFrequency(&wf);
					QueryPerformanceCounter(&w0);
					HDRenderWaterWetModels(ProbeWetLaraDraw,
						g_wet_stand_proj, 0);

					// -wetagain: its time to the GPU's end, apart.
					if (g_wet_again_split && SUCCEEDED(g_device->CreateQuery(
						D3DQUERYTYPE_EVENT, &e)) && e)
					{
						e->Issue(D3DISSUE_END);

						while (e->GetData(0, 0, D3DGETDATA_FLUSH) == S_FALSE)
						{
						}

						e->Release();
						QueryPerformanceCounter(&w1);
						printf("-wetagain: the models' warm draw cpu %.2f\n",
							(double)(w1.QuadPart - w0.QuadPart) * 1000.0 /
							(double)wf.QuadPart);
					}
				}
			}

			for (size_t i = 0; i + 2 < g_wet_stands.size(); i += 3)
				if (g_hdwater == 1 && !HDRenderWaterProbeStand(g_wet_stands[i],
					g_wet_stands[i + 1], g_wet_stands[i + 2],
					g_seed_origin[0], g_seed_origin[1], 0, g_wet_stand_alpha))
					printf("-wetstand %.0f,%.0f: no wet square there - not "
						"drawn\n", g_wet_stands[i], g_wet_stands[i + 1]);

			// A shot tank's wet floor, before the copy, as the engine has it.
			ProbeWetDraw(0);

			if (sheet)
				HDRenderWaterCopy();

			SpringCopy();

			/* The see-through half of the alpha pass - the glass - after
			 * everything opaque and before the glows, which is the order
			 * HDFrameDrawGlow draws them in. */
			DrawOneGroup(room, 0, &lighting, HD_PASS_BLEND);

			for (int i = 0; i < g_with_count; i++)
				DrawOneGroup(g_with_group[i], &withWorld[i], &lighting,
					HD_PASS_BLEND);

			DrawStandInHolders(&lighting, HD_PASS_BLEND);
			probestatics::DrawPieces(&lighting, HD_PASS_BLEND);

			DrawOneGroup(room, 0, &lighting, HD_PASS_ADDITIVE);

			for (int i = 0; i < g_with_count; i++)
				DrawOneGroup(g_with_group[i], &withWorld[i], &lighting,
					HD_PASS_ADDITIVE);

			DrawStandInHolders(&lighting, HD_PASS_ADDITIVE);
			probestatics::DrawPieces(&lighting, HD_PASS_ADDITIVE);

			if (g_mark_view && !HDRenderWaterMarkView())
				printf("-markview: the depth surface has no stencil\n");

			// The mark's own line for this frame, as the engine logs it.
			char markLine[256];

			if (!g_creatures.empty() && HDRenderWaterMarkLine(stem,
				markLine, sizeof(markLine)))
				printf("%s\n", markLine);
		}
	}

	const int calls = HDRenderDrawCalls();
	// The scissor rectangle, off again inside the bracket, as the engine's
	// frame does.
	HDRenderScissorOff();
	// The marble floors' mirror, before the wet floor's models.
	ProbeMarble(&projection);
	// The wet floor's mirror before the cards, as the engine has it.
	ProbeWetDraw(1);

	if (g_wet_again)
	{
		ProbeWetDraw(0);
		ProbeWetDraw(1);
	}
	// The water's cards where HDFrameDrawGlow draws them, and the springs.
	ProbeCardsDraw();
	SpringDraw();

	// -underlookset: the sea seen from below, over everything (a study).
	if (g_under_look)
	{
		double gpu = -1.0;
		const bool drawn = HDRenderWaterProbeLook(view, (float)persp,
			(float)width * 0.5f, (float)height * 0.5f,
			(float)cx + g_seed_origin[0], (float)cz + g_seed_origin[1],
			g_under_knobs, &gpu);

		printf("-underlookset: %s, GPU %.4f ms (timestamps round the pass) at "
			"%ldx%ld, knobs %.2f %.2f %.2f %.0f %.0f %.0f %.2f %.0f %.0f\n",
			drawn ? "drawn" : "NOT drawn (no frame under the water)", gpu,
			width, height, g_under_knobs[0], g_under_knobs[1],
			g_under_knobs[2], g_under_knobs[3], g_under_knobs[4],
			g_under_knobs[5], g_under_knobs[6], g_under_knobs[7],
			g_under_knobs[8]);
	}

	HDRenderFrameEnd();
	g_device->EndScene();

	/* The frame's time on the processor from its BeginScene to the GPU's
	 * end - what a first water frame's wait for the driver adds to
	 * (-nowarm against the warm-up). */
	if (g_hdwater >= 0)
	{
		IDirect3DQuery9* drained = 0;

		if (SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_EVENT, &drained)))
		{
			drained->Issue(D3DISSUE_END);

			while (drained->GetData(0, 0, D3DGETDATA_FLUSH) == S_FALSE) {}

			drained->Release();
		}

		LARGE_INTEGER f, c;

		QueryPerformanceFrequency(&f);
		QueryPerformanceCounter(&c);
		printf("-hdwater: the frame %.2f ms on the processor, BeginScene to "
			"the GPU's end\n", (double)(c.QuadPart - frameStart.QuadPart) *
			1000.0 / (double)f.QuadPart);
	}

	Take(after);

	if (g_scissor)
	{
		RECT box;

		box.left = g_scissor_box[0];
		box.top = g_scissor_box[1];
		box.right = g_scissor_box[2];
		box.bottom = g_scissor_box[3];
		ScissorProof(shot, box);
	}

	printf("HDRenderRoom returned %s after %d draw calls\n",
		drew ? "true" : "FALSE", calls);

	if (g_hdwater >= 0)
	{
		double ms = 0.0;
		int timed = 0;

		HDRenderWaterTiming(-1, &ms, &timed);
		printf("-hdwater: %d sheet draw(s), GPU %.3f ms (timestamps round "
			"the sheet draws)\n", timed, ms);
		HDRenderWaterCopyTiming(&ms, &timed);
		printf("-hdwater: %d frame copy(ies) %dx%d, GPU %.3f ms (timestamps "
			"round the StretchRect), depth texture %s\n", timed, (int)width,
			(int)height, ms, HDRenderWaterDepthBound() ? "bound" : "not bound");

		int segs = 0;

		HDRenderWaterFootTiming(&ms, &timed, &segs);
		printf("-hdwater: %d foot draw(s) carrying %d segment(s), GPU %.3f "
			"ms (timestamps round the feet's draws)\n", timed, segs, ms);
		HDRenderWaterRiverTiming(&ms, &timed);
		printf("-hdwater: %d river draw(s), GPU %.3f ms (timestamps round "
			"the rivers' streak draws)\n", timed, ms);
		HDRenderWaterFoamTiming(&ms, &timed);
		printf("-hdwater: %d foam draw(s), GPU %.3f ms (timestamps round "
			"the foam's own draws)\n", timed, ms);
	}

	if (g_roomwin_count)
		printf("decide: clip rooms=%d open=%d full=%d scissored=%d empty=%d "
			"pairs=%d whole=%d scissored=%d reach_rooms=%d reach_pieces=%d "
			"reach_whole=%d reach_hidden=%d unlinked_pieces=%d "
			"unlinked_drawn=%d unlinked_hidden=%d\n", g_decide_count.rooms,
			g_decide_count.open, g_decide_count.full, g_decide_count.scissored,
			g_decide_count.empty, g_decide_count.pairs,
			g_decide_count.pairsWhole, g_decide_count.pairsScissored,
			g_decide_count.reachRooms, g_decide_count.reachPieces,
			g_decide_count.reachWhole, g_decide_count.reachHidden,
			g_decide_count.unlinkedPieces, g_decide_count.unlinkedDrawn,
			g_decide_count.unlinkedHidden);

	/* Of those, the draw calls that turned a book, and the page the first
	 * of them showed - the book the engine's bookstep line follows. */
	{
		int group = -1, part = -1, page = 0;

		if (HDRenderBookWatch(&group, &part, &page))
			printf("book: %d draw call(s) turned a page; group %d part %d "
				"shows page %d\n", HDRenderBookCalls(), group, part, page);
	}

	// The end: whether SkyDrawPhase leaves the script's flat layers out
	// under this sky (HDRenderSkyCoversLayers).
	if (sky)
		printf("sky: %d sky group(s), %d unlit part(s), HDRenderSky returned "
			"%s, layers out %d\n", HDRenderSkyGroups(), stats.skyFlatParts,
			skyDrew ? "true" : "FALSE", HDRenderSkyCoversLayers() ? 1 : 0);

	const int broken = Diff(before, after);
	// kStateCount + three samplers' kSamplerCount + kStageCount, plus the
	// FVF, the six bound objects, stream 0's offset/stride, the scissor
	// rectangle (group M) and the light field's two textures.
	printf("state bracket: %d of %d watched states changed - %s\n",
		broken, kStateCount + 3 * kSamplerCount + kStageCount + 11,
		broken ? "FAIL, the classic path would find a different device"
			: "PASS, the device came back exactly as it went in");

	DrawClassicFaces(stem, room, view, projection, cx, cy, cz, width, height);

	sprintf(path, "%s_%s_r%d.png", out, stem, room);
	ShotWrite(shot, path);

	/* -gpu <n>: the GPU's time of the frame's solid room draws - every
	 * group of the frame, HD_PASS_SOLID, the passes that write depth and
	 * cost the most - drawn n times after one warm-up draw and timed with
	 * timestamps, after the picture is written so that the picture is the one
	 * without it; the median and the worst in ms, the draw calls of one
	 * draw, and the triangles of the groups drawn, all three passes. */
	if (g_gpu_runs > 0 && !skyOnly)
	{
		IDirect3DQuery9* q[4] = { 0, 0, 0, 0 };
		std::vector<double> times;
		int gpuCalls = 0;
		long long tris = HDRenderGroupTriangles(room, -1);

		for (int i = 0; i < g_with_count; i++)
			tris += HDRenderGroupTriangles(g_with_group[i], -1);

		g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &q[0]);
		g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1]);
		g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2]);
		g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &q[3]);

		for (int run = 0; run <= g_gpu_runs && q[0] && q[1] && q[2] && q[3];
			run++)
		{
			g_device->BeginScene();
			HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);
			q[0]->Issue(D3DISSUE_BEGIN);
			q[1]->Issue(D3DISSUE_END);
			DrawOneGroup(room, 0, &lighting, HD_PASS_SOLID);

			for (int i = 0; i < g_with_count; i++)
				DrawOneGroup(g_with_group[i], &withWorld[i], &lighting,
					HD_PASS_SOLID);

			q[2]->Issue(D3DISSUE_END);
			q[0]->Issue(D3DISSUE_END);
			q[3]->Issue(D3DISSUE_END);
			gpuCalls = HDRenderDrawCalls();
			HDRenderFrameEnd();
			g_device->EndScene();

			UINT64 a = 0, b = 0, f = 0;
			BOOL disjoint = TRUE;

			while (q[0]->GetData(&disjoint, sizeof(disjoint),
				D3DGETDATA_FLUSH) == S_FALSE)
				;

			while (q[1]->GetData(&a, sizeof(a), D3DGETDATA_FLUSH) == S_FALSE)
				;

			while (q[2]->GetData(&b, sizeof(b), D3DGETDATA_FLUSH) == S_FALSE)
				;

			while (q[3]->GetData(&f, sizeof(f), D3DGETDATA_FLUSH) == S_FALSE)
				;

			if (run > 0 && !disjoint && f)
				times.push_back((double)(b - a) * 1000.0 / (double)f);
		}

		for (int i = 0; i < 4; i++)
			if (q[i])
				q[i]->Release();

		std::sort(times.begin(), times.end());
		printf("-gpu: %d run(s) of the solid room draws, median %.3f ms, worst "
			"%.3f ms, %d draw call(s), %lld triangle(s) in %d group(s)\n",
			(int)times.size(), times.empty() ? -1.0 : times[times.size() / 2],
			times.empty() ? -1.0 : times.back(), gpuCalls, tris,
			g_with_count + 1);
	}

	/* -glow: the additive pass of every drawn group again, alone, onto
	 * black - first over the depth the full frame has just left behind, then
	 * over a cleared one. The first is the glow as the player sees it, cut
	 * by every wall, pillar and floor in front of it; the second is the same
	 * glow with nothing in front of it. Neither pass 2 nor the glass writes
	 * depth, so what the buffer holds is exactly the opaque and cut-out
	 * scene. Each variant is its own state bracket and each is checked like
	 * the full frame's. */
	if (g_glow_alone && !skyOnly)
	{
		static const char* suffix[2] = { "glow", "glow_nodepth" };

		for (int variant = 0; variant < 2; variant++)
		{
			Snapshot glowBefore, glowAfter;

			g_device->Clear(0, 0, variant == 0 ? D3DCLEAR_TARGET :
				D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
				D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

			PoseAsClassicPath();
			Take(glowBefore);
			g_device->BeginScene();
			HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);

			if (g_scissor)
				HDRenderScissor((int)g_scissor_box[0], (int)g_scissor_box[1],
					(int)g_scissor_box[2], (int)g_scissor_box[3]);

			DrawOneGroup(room, 0, &lighting, HD_PASS_ADDITIVE);

			for (int i = 0; i < g_with_count; i++)
				DrawOneGroup(g_with_group[i], &withWorld[i], &lighting,
					HD_PASS_ADDITIVE);

			HDRenderScissorOff();
			HDRenderFrameEnd();
			g_device->EndScene();
			Take(glowAfter);

			const int glowBroken = Diff(glowBefore, glowAfter);

			printf("state bracket (%s): %d changed - %s\n", suffix[variant],
				glowBroken, glowBroken ? "FAIL" : "PASS");

			sprintf(path, "%s_%s_r%d_%s.png", out, stem, room, suffix[variant]);
			ShotWrite(shot, path);
		}
	}

	ShotEnd(shot);

	if (allPasses)
	{
		static const char* names[3] = { "opaque", "alpha", "additive" };

		for (int pass = 0; pass < 3; pass++)
		{
			if (!ShotBegin(shot, width, height))
			{
				ShotEnd(shot);
				break;
			}

			g_device->BeginScene();
			HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);

			if (!skyOnly)
				HDRenderRoomPass(room, pass, 0, &lighting);

			HDRenderFrameEnd();
			g_device->EndScene();

			sprintf(path, "%s_%s_r%d_%d_%s.png", out, stem, room, pass, names[pass]);
			ShotWrite(shot, path);
			ShotEnd(shot);
		}
	}

	printf("\npeak commit of the whole run: %.1f MiB\n", PeakCommitMiB());

	HDRenderShutdown();
	DropDevice();
	return 0;
}

/* ------------------------------------------------- -crash, -devicecycle -- */

// The crash seam's writer here: the CRASH line on stdout.
static void CrashPrint(const char* line)
{
	printf("%s\n", line);
	fflush(stdout);
}

/* -crash <folder>: this tool once more, in <folder>, with -crashchild - the
 * seam installed exactly as WinMain installs it, then a read through a null
 * pointer. Then the line in crash.txt and the dump beside it are read back:
 * the line must begin with CRASH code=0xC0000005 (an access violation) and
 * name a module, the dump must exist and begin with MDMP. */
static int CrashTest(const char* self, const char* folder)
{
	char cmd[1024];
	char path[MAX_PATH];
	STARTUPINFOA si = {};
	PROCESS_INFORMATION pi = {};

	CreateDirectoryA(folder, 0);
	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\crash.txt", folder);
	DeleteFileA(path);
	si.cb = sizeof(si);
	_snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "\"%s\" -crashchild \"%s\"",
		self, folder);

	if (!CreateProcessA(0, cmd, 0, 0, FALSE, 0, 0, 0, &si, &pi))
	{
		printf("-crash: the child did not start\n");
		return 1;
	}

	WaitForSingleObject(pi.hProcess, 60000);

	DWORD code = 0;

	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	printf("-crash: the child ended with 0x%08lX\n", (unsigned long)code);

	FILE* in = 0;
	char line[512] = "";

	if (fopen_s(&in, path, "r") || !in || !fgets(line, sizeof(line), in))
	{
		if (in)
			fclose(in);

		printf("-crash: no crash.txt in %s - FAIL\n", folder);
		return 1;
	}

	fclose(in);
	printf("-crash: crash.txt says: %s", line);

	const bool lineOk = !strncmp(line, "CRASH code=0xC0000005 at=0x", 27) &&
		strstr(line, " module=") && !strstr(line, " module=?+");
	const char* dump = strstr(line, " dump=");
	bool dumpOk = false;

	if (dump && strncmp(dump + 6, "none", 4))
	{
		char name[MAX_PATH];
		char full[MAX_PATH];
		size_t n = strcspn(dump + 6, "\r\n");

		_snprintf_s(name, sizeof(name), _TRUNCATE, "%.*s", (int)n, dump + 6);
		_snprintf_s(full, sizeof(full), _TRUNCATE, "%s\\%s", folder, name);

		if (!fopen_s(&in, full, "rb") && in)
		{
			char head[4] = { 0, 0, 0, 0 };

			fseek(in, 0, SEEK_END);
			const long size = ftell(in);

			fseek(in, 0, SEEK_SET);
			dumpOk = fread(head, 1, 4, in) == 4 && !memcmp(head, "MDMP", 4);
			fclose(in);
			printf("-crash: %s, %ld bytes, header %s\n", name, size,
				dumpOk ? "MDMP" : "wrong");
		}
	}

	printf("-crash: line %s, dump %s\n", lineOk ? "ok" : "FAIL",
		dumpOk ? "ok" : "FAIL");
	return lineOk && dumpOk ? 0 : 1;
}

/* -cycleclassic <pages>: see the usage at the top. One window and one
 * IDirect3D9 for all three devices, as DXCreate keeps both across Alt+Enter;
 * each device made with the engine's flags (DXCreateD3DDevice: hardware
 * vertex processing, D3DCREATE_MULTITHREADED, windowed). No depth buffer:
 * the sky's draw type, the one in the crash, turns Z off. */
static const int kClassicSide = 256;	// the picture, one page's size

// D3DTLBUMPVERTEX and FVF (global/types.h): 40 bytes a vertex.
struct ClassicVertex
{
	float x, y, z, rhw;
	DWORD colour, specular;
	float tu, tv, tx, ty;
};

static const DWORD kClassicFvf = D3DFVF_TEX2 | D3DFVF_SPECULAR |
	D3DFVF_DIFFUSE | D3DFVF_XYZRHW;

static IDirect3DDevice9* ClassicDevice(IDirect3D9* d3d, HWND window)
{
	D3DPRESENT_PARAMETERS pp;
	IDirect3DDevice9* device = 0;

	memset(&pp, 0, sizeof(pp));
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = D3DFMT_X8R8G8B8;
	pp.BackBufferWidth = kClassicSide;
	pp.BackBufferHeight = kClassicSide;
	pp.BackBufferCount = 1;
	pp.hDeviceWindow = window;
	pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

	if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
		D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp,
		&device)))
		return 0;

	return device;
}

// A page with a pattern of its own, so that a page drawn in the wrong cell
// or not at all changes the picture.
static IDirect3DTexture9* ClassicPage(IDirect3DDevice9* device, int index)
{
	IDirect3DTexture9* page = 0;
	D3DLOCKED_RECT rect;

	if (FAILED(device->CreateTexture(kClassicSide, kClassicSide, 1, 0,
		D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &page, 0)))
		return 0;

	if (FAILED(page->LockRect(0, &rect, 0, 0)))
	{
		page->Release();
		return 0;
	}

	for (int y = 0; y < kClassicSide; y++)
	{
		DWORD* row = (DWORD*)((BYTE*)rect.pBits + y * rect.Pitch);

		for (int x = 0; x < kClassicSide; x++)
			row[x] = 0xFF000000 | ((x * (index + 1)) & 0xFF) << 16 |
				((y * (index + 3)) & 0xFF) << 8 |
				(((x ^ y) + index * 37) & 0xFF);
	}

	page->UnlockRect(0);
	return page;
}

/* One frame: pages[first..] each as one quad in its cell of a grid, two
 * triangles through DrawPrimitiveUP after SetTexture(0, page), with the
 * stage and render state HWInitialise sets (specific/function_table.cpp)
 * and the sky's Z off. */
static void ClassicFrame(IDirect3DDevice9* device,
	std::vector<IDirect3DTexture9*>& pages, int first)
{
	const int count = (int)pages.size();
	int grid = 1;

	while (grid * grid < count)
		grid++;

	const float cell = (float)kClassicSide / (float)grid;

	for (DWORD stage = 1; stage < 8; stage++)
		device->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_DISABLE);

	device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX,
		D3DTSS_TCI_PASSTHRU);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	device->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	device->SetRenderState(D3DRS_CLIPPING, FALSE);
	device->SetFVF(kClassicFvf);

	if (!first)
		device->Clear(0, 0, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);

	device->BeginScene();

	for (int i = first; i < count; i++)
	{
		const float x0 = cell * (float)(i % grid);
		const float y0 = cell * (float)(i / grid);
		const float x1 = x0 + cell;
		const float y1 = y0 + cell;
		ClassicVertex v[6] = {
			{ x0, y0, 0.5f, 1.0f, 0xFFFFFFFF, 0, 0.0f, 0.0f, 0.0f, 0.0f },
			{ x1, y0, 0.5f, 1.0f, 0xFFFFFFFF, 0, 1.0f, 0.0f, 0.0f, 0.0f },
			{ x0, y1, 0.5f, 1.0f, 0xFFFFFFFF, 0, 0.0f, 1.0f, 0.0f, 0.0f },
			{ x1, y0, 0.5f, 1.0f, 0xFFFFFFFF, 0, 1.0f, 0.0f, 0.0f, 0.0f },
			{ x1, y1, 0.5f, 1.0f, 0xFFFFFFFF, 0, 1.0f, 1.0f, 0.0f, 0.0f },
			{ x0, y1, 0.5f, 1.0f, 0xFFFFFFFF, 0, 0.0f, 1.0f, 0.0f, 0.0f } };

		device->SetTexture(0, pages[i]);

		const HRESULT drawn = device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2,
			v, sizeof(ClassicVertex));

		if (FAILED(drawn))
			printf("cycleclassic: page %d: DrawPrimitiveUP 0x%08lX\n", i,
				(unsigned long)drawn);
	}

	device->EndScene();
	device->SetTexture(0, 0);
}

// The whole picture, read back into `px` and written to `path`.
static bool ClassicDraw(IDirect3DDevice9* device,
	std::vector<IDirect3DTexture9*>& pages, std::vector<unsigned long>& px,
	const char* path)
{
	ClassicFrame(device, pages, 0);

	IDirect3DSurface9* back = 0;
	IDirect3DSurface9* read = 0;
	D3DLOCKED_RECT rect;
	bool ok = false;
	HRESULT copied = E_FAIL;

	if (SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO,
		&back)) && SUCCEEDED(device->CreateOffscreenPlainSurface(kClassicSide,
		kClassicSide, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &read, 0)) &&
		SUCCEEDED(copied = device->GetRenderTargetData(back, read)) &&
		SUCCEEDED(read->LockRect(&rect, 0, D3DLOCK_READONLY)))
	{
		px.resize(kClassicSide * kClassicSide);

		for (int y = 0; y < kClassicSide; y++)
			for (int x = 0; x < kClassicSide; x++)
				px[y * kClassicSide + x] = ((const DWORD*)((const BYTE*)
					rect.pBits + y * rect.Pitch))[x] & 0xFFFFFF;

		read->UnlockRect();
		ok = WritePng(path, &px[0], kClassicSide, kClassicSide);
	}
	else
		printf("cycleclassic: no picture, GetRenderTargetData 0x%08lX\n",
			(unsigned long)copied);

	if (read)
		read->Release();

	if (back)
		back->Release();

	return ok;
}

static int CycleClassic(int count, bool move, const char* out)
{
	WNDCLASSA cls;
	std::vector<IDirect3DTexture9*> pages;
	std::vector<unsigned long> picture[3];
	char path[MAX_PATH];
	bool ok = true;

	if (count < 1 || count > 256)
	{
		printf("cycleclassic: <pages> is 1 to 256, as Textures[] holds\n");
		return 1;
	}

	memset(&cls, 0, sizeof(cls));
	cls.lpfnWndProc = DefWindowProcA;
	cls.hInstance = GetModuleHandleA(0);
	cls.lpszClassName = "RoomProbeWindow";
	RegisterClassA(&cls);

	HWND window = CreateWindowA("RoomProbeWindow", "RoomProbe", WS_POPUP,
		-32, -32, 16, 16, 0, 0, cls.hInstance, 0);
	IDirect3D9* d3d = window ? Direct3DCreate9(D3D_SDK_VERSION) : 0;
	IDirect3DDevice9* device = d3d ? ClassicDevice(d3d, window) : 0;

	if (!device)
	{
		printf("cycleclassic: no device\n");
		return 1;
	}

	for (int i = 0; i < count; i++)
	{
		pages.push_back(ClassicPage(device, i));
		ok = ok && pages.back();
	}

	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s_classic1.png", out);
	ok = ClassicDraw(device, pages, picture[0], path) && ok;

	// One frame more with the last page alone, as the game draws its sky in
	// every frame and most pages not: the pages then differ in when they
	// were last drawn, which the runtime's bookkeeping of MANAGED textures
	// keeps per device.
	ClassicFrame(device, pages, count - 1);
	printf("cycleclassic: %d page(s) drawn on device 1: %s\n", count, path);

	for (int run = 1; run < 3 && device; run++)
	{
		printf("cycleclassic: device %d released, RefCnt = %lu\n", run,
			(unsigned long)device->Release());
		device = ClassicDevice(d3d, window);

		if (!device)
		{
			printf("cycleclassic: no device %d\n", run + 1);
			ok = false;
			break;
		}

		if (move)
		{
			DeviceMoveCount moved;

			DeviceMoveBegin(&moved);

			for (int i = 0; i < count; i++)
				DeviceMoveTexture(device, &pages[i], &moved);

			const long left = DeviceMoveEnd(&moved);

			printf("cycleclassic: device %d: moved=%ld dropped=%ld kept=%ld "
				"old_device_refs=%ld\n", run + 1, moved.moved, moved.dropped,
				moved.kept, left);
			ok = ok && moved.moved == count && left == 0;
		}
		else
		{
			CrashSeamInstall(CrashPrint, false, "RoomProbe");
			printf("cycleclassic: -nomove, the pages stay on device %d\n",
				run);
			fflush(stdout);
		}

		_snprintf_s(path, sizeof(path), _TRUNCATE, "%s_classic%d.png", out,
			run + 1);
		ok = ClassicDraw(device, pages, picture[run], path) && ok;
		printf("cycleclassic: drawn on device %d: %s, against device 1: %s\n",
			run + 1, path, picture[run] == picture[0] ? "identical" :
			"DIFFERENT");
		ok = ok && picture[run] == picture[0];
	}

	for (int i = 0; i < count; i++)
		if (pages[i])
			pages[i]->Release();

	if (device)
		device->Release();

	d3d->Release();
	DestroyWindow(window);
	printf("cycleclassic: %s\n", ok ? "ok" : "FAIL");
	return ok ? 0 : 1;
}

/* -windowcycle [-leak] [-out <prefix>]: the game window across Alt+Enter, on
 * a real window of the engine's class and styles, through the engine's own
 * window code (specific/devicewindow.h): the start as WinMain makes it
 * (WS_OVERLAPPEDWINDOW and WS_EX_APPWINDOW, hidden, then DXCreate's full
 * screen state and WinSetStyle), one toggle to the windowed state and one
 * back, each the way DXToggleFullScreen runs it (the old device released
 * first, the window placed, the device made, the style set). Each state
 * presents a colour of its own and prints its line (DeviceWindowStateText),
 * then what the system sees at the client area's middle - a GDI read of the
 * screen (BitBlt with CAPTUREBLT) and a frame of the DXGI desktop
 * duplication, held against the colour only while the window is the one at
 * that point (front=1; a program in the foreground keeps it behind) - and
 * the engine's screenshot path: DeviceReadBack's HRESULT and DeviceSaveTga's
 * bytes into <prefix>_state<n>.tga. -leak keeps a MANAGED texture of the
 * start device, as a texture nobody moves keeps its device alive. The
 * window covers the monitor for about two seconds. */
#include <d3d11.h>
#include <dxgi1_2.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")
#include "../../../TOMB5 Code/TOMB5/specific/devicewindow.h"

static void WindowCyclePump(DWORD ms)
{
	const DWORD until = GetTickCount() + ms;
	MSG msg;

	do
	{
		while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}

		Sleep(5);
	}
	while ((long)(until - GetTickCount()) > 0);
}

static void WindowCyclePresent(IDirect3DDevice9* device, D3DCOLOR colour,
	int frames)
{
	for (int i = 0; i < frames; i++)
	{
		device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, colour, 1.0f,
			0);
		device->Present(0, 0, 0, 0);
		WindowCyclePump(16);
	}
}

// DXCreate's way to a device, the old one already released, then the
// style WinSetStyle sets once DXCreate is back.
static IDirect3DDevice9* WindowCycleDevice(IDirect3D9* d3d, HWND window,
	bool full, DWORD* style)
{
	D3DPRESENT_PARAMETERS pp;
	IDirect3DDevice9* device = 0;
	long w = 0, h = 0;

	if (full)
	{
		RECT monitor;

		DeviceWindowFullRect(d3d, D3DADAPTER_DEFAULT, 1280, 720, &monitor, &w,
			&h);
		DeviceWindowShowFull(window, &monitor, w, h);
	}
	else
	{
		RECT viewport, screen;

		DeviceWindowPlaceWindowed(window, d3d, D3DADAPTER_DEFAULT, 1280, 720,
			*style, &viewport, &screen);
		w = viewport.right;
		h = viewport.bottom;
	}

	DevicePresentParams(&pp, window, w, h, true, true);

	if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
		D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp,
		&device)))
		return 0;

	*style = DeviceWindowStyle(window, full);
	return device;
}

// One pixel of the desktop as the DXGI duplication of the output that
// holds it delivers it, 0xFFFFFFFF when there is none.
static DWORD WindowCycleDuplicate(IDirect3DDevice9* device, POINT at)
{
	ID3D11Device* d11 = 0;
	ID3D11DeviceContext* context = 0;
	IDXGIDevice* dxgi = 0;
	IDXGIAdapter* adapter = 0;
	IDXGIOutput* output = 0;
	IDXGIOutput1* output1 = 0;
	IDXGIOutputDuplication* dup = 0;
	DWORD pixel = 0xFFFFFFFF;
	DXGI_OUTPUT_DESC desc;

	if (FAILED(D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, 0, 0,
		D3D11_SDK_VERSION, &d11, 0, &context)))
		return pixel;

	d11->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi);

	if (dxgi)
		dxgi->GetAdapter(&adapter);

	for (UINT i = 0; adapter && !output1; i++)
	{
		if (FAILED(adapter->EnumOutputs(i, &output)))
			break;

		output->GetDesc(&desc);

		if (PtInRect(&desc.DesktopCoordinates, at))
			output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);

		output->Release();
		output = 0;
	}

	if (output1 && SUCCEEDED(output1->DuplicateOutput(d11, &dup)))
	{
		for (int tries = 0; tries < 20 && pixel == 0xFFFFFFFF; tries++)
		{
			DXGI_OUTDUPL_FRAME_INFO info;
			IDXGIResource* resource = 0;

			// A frame of ours is presented so that the duplication has a
			// new desktop image to hand over.
			device->Present(0, 0, 0, 0);

			if (FAILED(dup->AcquireNextFrame(100, &info, &resource)))
				continue;

			ID3D11Texture2D* frame = 0;

			resource->QueryInterface(__uuidof(ID3D11Texture2D),
				(void**)&frame);

			if (frame && info.LastPresentTime.QuadPart)
			{
				D3D11_TEXTURE2D_DESC td;
				ID3D11Texture2D* staging = 0;

				frame->GetDesc(&td);
				td.Usage = D3D11_USAGE_STAGING;
				td.BindFlags = 0;
				td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				td.MiscFlags = 0;

				if (SUCCEEDED(d11->CreateTexture2D(&td, 0, &staging)))
				{
					D3D11_MAPPED_SUBRESOURCE map;

					context->CopyResource(staging, frame);

					if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0,
						&map)))
					{
						const long x = at.x - desc.DesktopCoordinates.left;
						const long y = at.y - desc.DesktopCoordinates.top;

						pixel = ((const DWORD*)((const BYTE*)map.pData +
							y * map.RowPitch))[x] & 0xFFFFFF;
						context->Unmap(staging, 0);
					}

					staging->Release();
				}
			}

			if (frame)
				frame->Release();

			resource->Release();
			dup->ReleaseFrame();
		}

		dup->Release();
	}

	if (output1) output1->Release();
	if (adapter) adapter->Release();
	if (dxgi) dxgi->Release();
	if (context) context->Release();
	if (d11) d11->Release();
	return pixel;
}

// What the system sees of a state and what the engine's screenshot path
// makes of it; true when all of it shows `colour`.
static bool WindowCycleLook(HWND window, IDirect3DDevice9* device,
	D3DCOLOR colour, const char* prefix, int state, std::string* text)
{
	DeviceWindowState s;
	char line[1024];
	char path[MAX_PATH];
	const DWORD want = colour & 0xFFFFFF;

	DeviceWindowMeasure(window, device, &s);
	DeviceWindowStateText(&s, line, sizeof(line));
	*text = line;
	printf("windowcycle: state %d %s\n", state, line);

	POINT at;

	at.x = (s.client.left + s.client.right) / 2;
	at.y = (s.client.top + s.client.bottom) / 2;

	HDC screen = GetDC(0);
	HDC mem = CreateCompatibleDC(screen);
	HBITMAP one = CreateCompatibleBitmap(screen, 1, 1);
	HGDIOBJ was = SelectObject(mem, one);

	BitBlt(mem, 0, 0, 1, 1, screen, at.x, at.y, SRCCOPY | CAPTUREBLT);

	const COLORREF g = GetPixel(mem, 0, 0);
	const DWORD gdi = (GetRValue(g) << 16) | (GetGValue(g) << 8) | GetBValue(g);

	SelectObject(mem, was);
	DeleteObject(one);
	DeleteDC(mem);
	ReleaseDC(0, screen);

	// The screen shows this window there only when nothing stands in front
	// of it: another program in the foreground keeps it behind.
	const bool front = WindowFromPoint(at) == window;

	const DWORD dup = WindowCycleDuplicate(device, at);
	IDirect3DSurface9* back = 0;
	const HRESULT hr = DeviceReadBack(device, &back);
	long bytes = 0;

	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s_state%d.tga", prefix,
		state);

	if (back)
	{
		bytes = DeviceSaveTga(back, path);
		back->Release();
	}

	printf("windowcycle: state %d at %ld,%ld front=%d want %06lX gdi %06lX "
		"duplication %06lX readback 0x%08lX file %s %ld bytes\n", state, at.x,
		at.y, (int)front, want, gdi, dup, (unsigned long)hr, path, bytes);
	return SUCCEEDED(hr) && bytes > 0 && (!front || (gdi == want &&
		dup == want));
}

static int WindowCycle(bool leak, const char* prefix)
{
	WNDCLASSA cls;
	static const D3DCOLOR colour[3] = { 0xFFC02020, 0xFF20C020, 0xFF2020C0 };
	std::string text[3];
	bool seen[3] = { false, false, false };

	memset(&cls, 0, sizeof(cls));
	cls.style = CS_VREDRAW | CS_HREDRAW;
	cls.lpfnWndProc = DefWindowProcA;
	cls.hInstance = GetModuleHandleA(0);
	cls.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	cls.hCursor = LoadCursor(0, IDC_ARROW);
	cls.lpszClassName = "RoomProbeGameWindow";
	RegisterClassA(&cls);

	// WinCreateWindow, then WinMain's hide before the device.
	HWND window = CreateWindowExA(WS_EX_APPWINDOW, "RoomProbeGameWindow",
		"RoomProbe window cycle", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, cls.hInstance, 0);
	IDirect3D9* d3d = window ? Direct3DCreate9(D3D_SDK_VERSION) : 0;

	if (!d3d)
	{
		printf("windowcycle: no window\n");
		return 1;
	}

	ShowWindow(window, SW_HIDE);
	UpdateWindow(window);

	DWORD style = WS_OVERLAPPEDWINDOW;
	IDirect3DDevice9* device = WindowCycleDevice(d3d, window, true, &style);
	IDirect3DTexture9* kept = 0;

	// WinMain after DXCreate and WinSetStyle.
	UpdateWindow(window);
	ShowWindow(window, SW_SHOWNORMAL);

	for (int state = 0; state < 3 && device; state++)
	{
		WindowCyclePresent(device, colour[state], 20);
		WindowCyclePump(300);
		seen[state] = WindowCycleLook(window, device, colour[state], prefix,
			state + 1, &text[state]);

		if (state == 2)
			break;

		if (leak && !kept)
			device->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8,
				D3DPOOL_MANAGED, &kept, 0);

		// DXToggleFullScreen: DXCreate releases the device, then places.
		printf("windowcycle: device released, RefCnt = %lu\n",
			(unsigned long)device->Release());
		device = WindowCycleDevice(d3d, window, state == 1, &style);
	}

	if (kept)
		printf("windowcycle: the kept texture's device released, RefCnt = "
			"%lu\n", (unsigned long)kept->Release());

	const bool same = device && text[0] == text[2];

	printf("windowcycle: start against after two toggles: %s\n",
		same ? "identical" : "DIFFERENT");
	printf("windowcycle: read back and seen where in front: start %s, one "
		"toggle %s, two %s\n", seen[0] ? "yes" : "NO",
		device && seen[1] ? "yes" : "NO", device && seen[2] ? "yes" : "NO");

	if (device)
		device->Release();

	d3d->Release();
	DestroyWindow(window);
	return same && seen[0] && seen[1] && seen[2] ? 0 : 1;
}

/* -reloadas: after a run whose key the other level's refused
 * (ProbeReload), the same run again for that level in this process -
 * the switches as given, the level replaced, the reload switches left out -
 * so that its build gets the room inputs any run hands in out of -roombox
 * and starts where the engine's next build starts, after the release of
 * the level before. The containers a run fills by push_back or resize are
 * emptied first, as -devicecycle empties them between its runs. */
static int ProbeReloadOther(int result, int argc, char** argv)
{
	if (!g_reload_refused || argc < 4)
		return result;

	const std::string other = g_reload_as;
	std::vector<char*> a;

	for (int i = 0; i < argc; i++)
	{
		if (!strcmp(argv[i], "-reloadas") && i + 1 < argc)
		{
			i++;
			continue;
		}

		if (!strcmp(argv[i], "-reload") || !strcmp(argv[i], "-ahead") ||
			!strcmp(argv[i], "-aheadverify") || !strcmp(argv[i], "-loadtime"))
			continue;

		a.push_back(i == 2 ? (char*)other.c_str() : argv[i]);
	}

	g_reload = false;
	g_reload_as.clear();
	g_reload_kept = false;
	g_reload_refused = false;
	g_reload_rebuild = true;
	g_loadtime = false;
	g_ahead_pics = false;

	g_ripples.clear();
	g_lara_set = false;
	g_feet.clear();
	g_creatures.clear();
	g_waders.clear();
	g_swimmers.clear();
	g_creature_mark = true;
	g_mark_view = false;
	g_classic_rooms.clear();
	g_faces_built.clear();
	g_cuts_built.clear();
	g_stand_draws.clear();
	g_intrusions_built.clear();
	g_stand_faces_built.clear();
	g_stand_holders_built.clear();
	g_stand_covered_room.clear();
	g_stand_covered_answer.clear();
	g_stand_carrier.clear();
	g_stand_flags.clear();
	g_with_count = 0;
	g_roomwin_count = 0;
	// And the lists the -roombox read sizes per room, emptied whole.
	g_boxes_built.clear();
	g_grids_built.clear();
	g_sectors_built.clear();
	g_air_built.clear();
	g_air_links_built.clear();
	g_air_sectors_built.clear();
	g_stand_rooms_built.clear();
	/* And the statics, which a run reads once (probestatics::Load): read
	 * again for the other level, and -static's pairs parsed again. */
	probestatics::g_list.clear();
	probestatics::g_forced.clear();
	probestatics::g_loaded = false;
	probestatics::g_answered = false;

	printf("\n-reload: the run again for %s in this process\n", other.c_str());
	return ProbeMain((int)a.size(), &a[0]);
}

/* -viscams <file>: the walk at every camera of a camera file, a set in one
 * call - RoomProbe <5-folder> -viscams <file> -roombox <folder> [switches].
 * A line of the file: <name> <LEVEL> <room> <x> <y> <z> (world, the room
 * the eye stands in), then `yaw <degrees> <pitch>` or `target <x> <y> <z>`,
 * then `size <w> <h>` (1280 720) and `flip <room,...>` if wanted; `#`
 * starts a comment. Per camera the tool runs itself on the walk (-walk,
 * -walkfinal, -walkeach, -nodraw, -fov 80 and every switch after the
 * file, -vis 1 among them) and prints
 *
 *   viscams: <name> <LEVEL> rooms=<n> list=<room>:<source>,...
 *
 * the source the walk names per room, in -walkfinal's words: walk, box (a
 * guest), opening, portal (a wide portal), edge.
 * The lines of two builds side by side name every camera a rule changes;
 * the pictures are drawn with -roomwin from the same lists. */
static int VisCams(int argc, char** argv, int at)
{
	FILE* f = fopen(argv[at + 1], "r");
	char exe[MAX_PATH];
	char line[1024];
	int cams = 0, failed = 0;

	if (!f || argc < 2)
	{
		printf("viscams: cannot read %s\n", argv[at + 1]);
		return 1;
	}

	GetModuleFileNameA(0, exe, sizeof(exe));

	// Every switch but the file's own two words goes to each walk.
	std::string rest;

	for (int i = 2; i < argc; i++)
		if (i != at && i != at + 1)
			rest += std::string(" \"") + argv[i] + "\"";

	while (fgets(line, sizeof(line), f))
	{
		char* hash = strchr(line, '#');

		if (hash)
			*hash = 0;

		char name[64], level[32], kind[16];
		int room = 0, w = 1280, h = 720, used = 0;
		double x, y, z, a, b, c = 0.0;

		if (sscanf(line, "%63s %31s %d %lf %lf %lf %15s %n", name, level,
			&room, &x, &y, &z, kind, &used) != 7)
			continue;

		std::string cam;
		const char* more = line + used;
		int n = 0;

		if (!strcmp(kind, "target") &&
			sscanf(more, "%lf %lf %lf %n", &a, &b, &c, &n) == 3)
			cam = " -walktarget " + std::to_string((long)a) + " " +
				std::to_string((long)b) + " " + std::to_string((long)c);
		else if (!strcmp(kind, "yaw") &&
			sscanf(more, "%lf %lf %n", &a, &b, &n) == 2)
			cam = " -walkyaw " + std::to_string(a) + " " +
				std::to_string(a) + " 1 -walkpitch " + std::to_string(b);
		else
		{
			printf("viscams: %s: no camera\n", name);
			failed++;
			continue;
		}

		more += n;

		char word[16], flips[256];
		std::string flip;

		while (sscanf(more, "%15s %n", word, &n) == 1)
		{
			more += n;

			if (!strcmp(word, "size") &&
				sscanf(more, "%d %d %n", &w, &h, &n) == 2)
				more += n;
			else if (!strcmp(word, "flip") &&
				sscanf(more, "%255s %n", flips, &n) == 1)
			{
				flip = std::string(" -flip ") + flips;
				more += n;
			}
			else
				break;
		}

		const std::string cmd = std::string("\"\"") + exe + "\" \"" + argv[1] +
			"\" " + level + " " + std::to_string(room) + rest + " -walk " +
			std::to_string(room) + " " + std::to_string((long)x) + " " +
			std::to_string((long)y) + " " + std::to_string((long)z) +
			" -size " + std::to_string(w) + " " + std::to_string(h) +
			" -fov 80 -walkfinal -walkeach -nodraw" + cam + flip + "\"";
		FILE* p = _popen(cmd.c_str(), "r");
		std::vector<int> order;
		std::vector<std::string> source(32768);
		char out[2048];

		cams++;

		while (p && fgets(out, sizeof(out), p))
		{
			const char* rooms = strstr(out, " rooms=");
			int r = 0, got = 0;
			char vis[16];

			if (!strncmp(out, "  yaw", 5) && rooms && strchr(rooms, ':'))
			{
				const char* s = strchr(rooms, ':') + 1;

				order.clear();

				while (sscanf(s, "%d %n", &r, &n) == 1)
				{
					order.push_back(r);
					s += n;
				}
			}
			else if (sscanf(out, " room %d final=%n", &r, &got) == 1 &&
				got > 0 && r >= 0 && r < (int)source.size())
			{
				const char* v = strstr(out, " vis=");

				source[(size_t)r] = v && sscanf(v + 5, "%15s", vis) == 1 ?
					vis : "-";
			}
		}

		if (p)
			_pclose(p);

		if (order.empty())
		{
			printf("viscams: %s %s no walk\n", name, level);
			failed++;
			continue;
		}

		printf("viscams: %s %s rooms=%d list=", name, level, (int)order.size());

		for (size_t k = 0; k < order.size(); k++)
			printf("%s%d:%s", k ? "," : "", order[k],
				source[(size_t)order[k]].empty() ? "-" :
				source[(size_t)order[k]].c_str());

		printf("\n");
	}

	fclose(f);
	printf("viscams: %d cameras, %d without a walk\n", cams, failed);
	return failed ? 1 : 0;
}

/* The picture a run of this tool wrote (WritePng: one IDAT, filter 0 on
 * every row, 24 bit) read back into `rgb`. False for any other file. */
static bool ReadOwnPng(const std::string& path, std::vector<unsigned char>& rgb,
	long* w, long* h)
{
	FILE* f = fopen(path.c_str(), "rb");

	if (!f)
		return false;

	std::vector<unsigned char> file;
	unsigned char buf[65536];
	size_t got;

	while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
		file.insert(file.end(), buf, buf + got);

	fclose(f);

	const auto be = [&](size_t at)
	{
		return ((unsigned long)file[at] << 24) | ((unsigned long)file[at + 1] <<
			16) | ((unsigned long)file[at + 2] << 8) | file[at + 3];
	};
	std::vector<unsigned char> packed;
	size_t at = 8;

	*w = *h = 0;

	while (at + 12 <= file.size())
	{
		const unsigned long len = be(at);

		if (at + 12 + len > file.size())
			return false;

		if (!memcmp(&file[at + 4], "IHDR", 4) && len >= 13)
		{
			*w = (long)be(at + 8);
			*h = (long)be(at + 12);
		}
		else if (!memcmp(&file[at + 4], "IDAT", 4))
			packed.insert(packed.end(), file.begin() + at + 8,
				file.begin() + at + 8 + len);

		at += 12 + len;
	}

	if (*w <= 0 || *h <= 0 || packed.empty())
		return false;

	std::vector<unsigned char> raw((size_t)*h * (1 + (size_t)*w * 3));
	uLongf length = (uLongf)raw.size();

	if (uncompress(&raw[0], &length, &packed[0], (uLong)packed.size()) !=
		Z_OK || length != raw.size())
		return false;

	rgb.resize((size_t)*w * *h * 3);

	for (long y = 0; y < *h; y++)
	{
		if (raw[(size_t)y * (1 + (size_t)*w * 3)] != 0)
			return false;

		memcpy(&rgb[(size_t)y * *w * 3],
			&raw[(size_t)y * (1 + (size_t)*w * 3) + 1], (size_t)*w * 3);
	}

	return true;
}

/* -undertick <file> <room>: the surface seen from below, counted along a
 * walk of eyes - a set in one call: RoomProbe <5-folder> -undertick <file>
 * <room> -roombox <folder> -out <prefix> [switches]. A line of the file:
 * <name> <LEVEL> <room> <x> <y> <z> target <tx> <ty> <tz> (world, the room
 * the eye stands in; a CAMLOG tick's eye= and target=); `#` starts a
 * comment. Per eye the tool runs itself three times: the walk (-walk,
 * -walktarget, -walkfinal, -nodraw, -fov 80 and every switch after the
 * file, -vis 1 among them, as -viscams), then the picture from that eye
 * with the walk's rooms (-with, the eye's room the seed, -underwater and
 * the camera of -walktarget), once whole and once without <room> and
 * without the switches that draw the look (-underlook, -underlookknobs,
 * -underlookset) - the sea's surface art and the look left out - and
 * prints
 *
 *   undertick: <name> room=<r> rooms=<n> listed=<0|1> surface=<pixels>
 *   share=<per cent> light=<mean luma of the difference>
 *
 * the surface's pixels those that differ by 4 or more of 255 in any
 * channel; listed says whether the walk lists <room>. The pictures stay
 * under <prefix>_<name>_s and _n.
 *
 * With -surfacealways the first picture takes the game's rule for the
 * sea's surface (hd_level.cpp, HDFrameDraw; the picture's -surfacealways):
 * where the walk leaves <room> out - which the rule names in JOBY4 - the
 * line ends in carried=1, and carried=0 where the walk lists it or the
 * switch is not given. */
static int UnderTicks(int argc, char** argv, int at)
{
	FILE* f = at + 2 < argc ? fopen(argv[at + 1], "r") : 0;
	const int surface = at + 2 < argc ? atoi(argv[at + 2]) : -1;
	char exe[MAX_PATH];
	char line[1024];
	std::string folder, prefix = "undertick";
	int eyes = 0, failed = 0;

	if (!f || argc < 2)
	{
		printf("undertick: cannot read %s\n", at + 1 < argc ? argv[at + 1] :
			"(no file)");
		return 1;
	}

	GetModuleFileNameA(0, exe, sizeof(exe));

	// The switches for the walk and for the two pictures.
	std::string walkRest, pictureRest, bareRest;
	bool look = false, sky = false, rooms = false, always = false;

	for (int i = 2; i < argc; i++)
	{
		if (i >= at && i <= at + 2)
			continue;

		const std::string a = argv[i];

		look = look || a == "-underlook" || a == "-underlookknobs" ||
			a == "-underlookset";
		sky = sky || a == "-sky";
		rooms = rooms || a == "-rooms";

		if (a == "-out" && i + 1 < argc)
		{
			prefix = argv[++i];
			continue;
		}

		// The game's rule, for the first picture alone.
		if (a == "-surfacealways")
		{
			always = true;
			pictureRest += " \"" + a + "\"";
			continue;
		}

		if (a == "-roombox" && i + 1 < argc)
			folder = argv[i + 1];

		const std::string q = " \"" + a + "\"";
		int skip = a == "-underlookknobs" ? HD_LOOK_KNOBS :
			a == "-underlookset" ? 9 : 0;

		walkRest += q;
		pictureRest += q;

		if (a != "-underlook" && !skip)
			bareRest += q;

		for (; skip > 0 && i + 1 < argc; skip--)
		{
			const std::string v = std::string(" \"") + argv[++i] + "\"";

			walkRest += v;
			pictureRest += v;
		}
	}

	/* The look is drawn with the sky, and the sky's groups come in with the
	 * rooms' load: without -sky and -rooms <n> it is left out unseen and
	 * the counts are those of a picture without it. */
	if (look && !(sky && rooms))
	{
		printf("undertick: the look needs -sky and -rooms <n>\n");
		fclose(f);
		return 1;
	}

	std::string loaded;

	while (fgets(line, sizeof(line), f))
	{
		char* hash = strchr(line, '#');

		if (hash)
			*hash = 0;

		char name[64], level[32], kind[16];
		int room = 0;
		long x, y, z, tx, ty, tz;

		if (sscanf(line, "%63s %31s %d %ld %ld %ld %15s %ld %ld %ld", name,
			level, &room, &x, &y, &z, kind, &tx, &ty, &tz) != 10 ||
			strcmp(kind, "target"))
			continue;

		if (loaded != level)
		{
			if (folder.empty() || !walkoff::Load(folder, level))
			{
				printf("undertick: %s needs -roombox with %s.TRC\n", name,
					level);
				failed++;
				continue;
			}

			loaded = level;
		}

		if (room < 0 || room >= (int)walkoff::g_room.size())
		{
			printf("undertick: %s: room %d is not in %s\n", name, room, level);
			failed++;
			continue;
		}

		eyes++;

		const std::string target = " -walktarget " + std::to_string(tx) +
			" " + std::to_string(ty) + " " + std::to_string(tz);
		const std::string walk = std::string("\"\"") + exe + "\" \"" +
			argv[1] + "\" " + level + " " + std::to_string(room) + walkRest +
			" -walk " + std::to_string(room) + " " + std::to_string(x) + " " +
			std::to_string(y) + " " + std::to_string(z) + target +
			" -fov 80 -walkfinal -walkeach -nodraw\"";
		FILE* p = _popen(walk.c_str(), "r");
		std::vector<int> order;
		char out[2048];
		int n = 0;

		while (p && fgets(out, sizeof(out), p))
		{
			const char* rooms = strstr(out, " rooms=");

			if (!strncmp(out, "  yaw", 5) && rooms && strchr(rooms, ':'))
			{
				const char* s = strchr(rooms, ':') + 1;
				int r = 0;

				order.clear();

				while (sscanf(s, "%d %n", &r, &n) == 1)
				{
					order.push_back(r);
					s += n;
				}
			}
		}

		if (p)
			_pclose(p);

		if (order.empty())
		{
			printf("undertick: %s %s no walk\n", name, level);
			failed++;
			continue;
		}

		// The walk's rooms at their offsets from the eye's room.
		const walkoff::Room& seed = walkoff::g_room[(size_t)room];
		std::string with, bareWith;
		bool listed = false;

		for (int r : order)
		{
			if (r == room || r < 0 || r >= (int)walkoff::g_room.size())
				continue;

			const walkoff::Room& o = walkoff::g_room[(size_t)r];
			const std::string w = " -with " + std::to_string(r) + " " +
				std::to_string(o.x - seed.x) + " 0 " +
				std::to_string(o.z - seed.z);

			with += w;

			if (r == surface)
				listed = true;
			else
				bareWith += w;
		}

		if (room == surface)
			listed = true;

		const bool carried = always && !listed;

		const std::string eye = std::string("\"\"") + exe + "\" \"" +
			argv[1] + "\" " + level + " " + std::to_string(room) + " " +
			std::to_string(x - seed.x) + " " + std::to_string(y) + " " +
			std::to_string(z - seed.z) + " 0 0" + target + " -underwater";
		const std::string sPath = prefix + "_" + name + "_s";
		const std::string nPath = prefix + "_" + name + "_n";
		const std::string sCmd = eye + with + pictureRest + " -out \"" +
			sPath + "\" > NUL 2>&1\"";
		const std::string nCmd = eye + bareWith + bareRest + " -out \"" +
			nPath + "\" > NUL 2>&1\"";

		system(sCmd.c_str());
		system(nCmd.c_str());

		const std::string tail = std::string("_") + level + "_r" +
			std::to_string(room) + ".png";
		std::vector<unsigned char> a, b;
		long aw = 0, ah = 0, bw = 0, bh = 0;

		if (!ReadOwnPng(sPath + tail, a, &aw, &ah) ||
			!ReadOwnPng(nPath + tail, b, &bw, &bh) || aw != bw || ah != bh)
		{
			printf("undertick: %s %s no pictures\n", name, level);
			failed++;
			continue;
		}

		long pixels = 0;
		double light = 0.0;

		for (size_t i = 0; i + 2 < a.size(); i += 3)
		{
			int most = 0;
			double luma = 0.0;

			for (int c = 0; c < 3; c++)
			{
				const int d = abs((int)a[i + c] - (int)b[i + c]);

				most = d > most ? d : most;
				luma += d * (c == 0 ? 0.299 : c == 1 ? 0.587 : 0.114);
			}

			if (most >= 4)
			{
				pixels++;
				light += luma;
			}
		}

		printf("undertick: %s room=%d rooms=%d listed=%d surface=%ld "
			"share=%.2f light=%.2f carried=%d\n", name, room,
			(int)order.size(), listed ? 1 : 0, pixels,
			100.0 * pixels / (double)(aw * ah), pixels ? light / pixels : 0.0,
			carried ? 1 : 0);
	}

	fclose(f);
	printf("undertick: %d eyes, %d without a count\n", eyes, failed);
	return failed ? 1 : 0;
}

int main(int argc, char** argv)
{
	for (int i = 2; i + 1 < argc; i++)
		if (!strcmp(argv[i], "-viscams"))
			return VisCams(argc, argv, i);

	for (int i = 2; i + 2 < argc; i++)
		if (!strcmp(argv[i], "-undertick"))
			return UnderTicks(argc, argv, i);

	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "-windowcycle"))
		{
			bool leak = false;
			const char* prefix = ".\roomprobe";

			for (int j = 1; j < argc; j++)
			{
				if (!strcmp(argv[j], "-leak"))
					leak = true;
				else if (!strcmp(argv[j], "-out") && j + 1 < argc)
					prefix = argv[j + 1];
			}

			return WindowCycle(leak, prefix);
		}

	for (int i = 1; i + 1 < argc; i++)
	{
		if (!strcmp(argv[i], "-crashchild"))
		{
			SetCurrentDirectoryA(argv[i + 1]);
			CrashSeamInstall(CrashPrint, true, "PCtomb5");

			volatile int* nothing = 0;

			return *nothing;
		}

		if (!strcmp(argv[i], "-crash"))
			return CrashTest(argv[0], argv[i + 1]);

		if (!strcmp(argv[i], "-cycleclassic"))
		{
			bool move = true;
			const char* prefix = ".\\roomprobe";

			for (int j = 1; j < argc; j++)
			{
				if (!strcmp(argv[j], "-nomove"))
					move = false;
				else if (!strcmp(argv[j], "-out") && j + 1 < argc)
					prefix = argv[j + 1];
			}

			return CycleClassic(atoi(argv[i + 1]), move, prefix);
		}
	}

	std::vector<char*> args;
	bool cycle = false;
	const char* out = ".\\roomprobe";

	for (int i = 0; i < argc; i++)
	{
		if (!strcmp(argv[i], "-devicecycle"))
		{
			cycle = true;
			continue;
		}

		if (!strcmp(argv[i], "-out") && i + 1 < argc)
		{
			out = argv[++i];
			continue;
		}

		args.push_back(argv[i]);
	}

	if (!cycle || argc < 4)
		return ProbeReloadOther(ProbeMain(argc, argv), argc, argv);

	/* Three runs, each with its own device (ProbeMain makes it and drops it
	 * after HDRenderShutdown). The containers a run fills by push_back are
	 * emptied before the next, so the third run starts where the first
	 * did. */
	static const char* const suffix[3] = { "_cycle1", "_cycle2", "_cycle3" };
	char prefix[3][MAX_PATH];
	int result = 0;

	g_cycle_report = true;

	for (int run = 0; run < 3; run++)
	{
		std::vector<char*> a = args;
		char classicRoom[16];

		_snprintf_s(prefix[run], MAX_PATH, _TRUNCATE, "%s%s", out,
			suffix[run]);
		a.push_back((char*)"-out");
		a.push_back(prefix[run]);

		if (run == 1)
		{
			strcpy_s(classicRoom, sizeof(classicRoom), argv[3]);
			a.push_back((char*)"-classicroom");
			a.push_back(classicRoom);
			a.push_back((char*)"-classiconly");
		}

		g_ripples.clear();
		g_lara_set = false;
		g_feet.clear();
		g_creatures.clear();
		g_waders.clear();
		g_swimmers.clear();
		g_creature_mark = true;
		g_mark_view = false;
		g_classic_rooms.clear();
		g_faces_built.clear();
		g_cuts_built.clear();
		g_stand_draws.clear();
		// The -roombox lists, filled by push_back while the level is read.
		g_intrusions_built.clear();
		g_stand_faces_built.clear();
		g_stand_holders_built.clear();
		g_stand_covered_room.clear();
		g_stand_covered_answer.clear();
		g_stand_carrier.clear();
		g_stand_flags.clear();
		// And the groups -with and -roomwin add, counted per argument.
		g_with_count = 0;
		g_roomwin_count = 0;
		printf("\ndevicecycle: run %d of 3 (%s)\n", run + 1,
			run == 1 ? "classic" : "HD");
		result |= ProbeMain((int)a.size(), &a[0]);
	}

	/* The first and the third HD picture, byte for byte: every PNG the
	 * first run wrote under its prefix against the third's of the same
	 * name. */
	WIN32_FIND_DATAA found;
	char pattern[MAX_PATH];
	int pairs = 0;
	int same = 0;

	_snprintf_s(pattern, sizeof(pattern), _TRUNCATE, "%s*.png", prefix[0]);

	HANDLE find = FindFirstFileA(pattern, &found);
	const char* slash = strrchr(prefix[0], '\\');
	const size_t dirLen = slash ? (size_t)(slash - prefix[0] + 1) : 0;
	const size_t tail = strlen(slash ? slash + 1 : prefix[0]);

	while (find != INVALID_HANDLE_VALUE)
	{
		char first[MAX_PATH];
		char third[MAX_PATH];
		std::vector<unsigned char> bytes[2];

		_snprintf_s(first, sizeof(first), _TRUNCATE, "%.*s%s", (int)dirLen,
			prefix[0], found.cFileName);
		_snprintf_s(third, sizeof(third), _TRUNCATE, "%s%s", prefix[2],
			found.cFileName + tail);

		for (int k = 0; k < 2; k++)
		{
			FILE* f = 0;

			if (!fopen_s(&f, k ? third : first, "rb") && f)
			{
				int c;

				while ((c = fgetc(f)) != EOF)
					bytes[k].push_back((unsigned char)c);

				fclose(f);
			}
		}

		pairs++;
		same += !bytes[0].empty() && bytes[0] == bytes[1];
		printf("devicecycle: %s against %s: %s\n", first, third,
			!bytes[0].empty() && bytes[0] == bytes[1] ? "identical" :
			"DIFFERENT");

		if (!FindNextFileA(find, &found))
		{
			FindClose(find);
			break;
		}
	}

	printf("devicecycle: %d of %d HD picture(s) identical after the cycle\n",
		same, pairs);
	return result || !pairs || same != pairs ? 1 : 0;
}
