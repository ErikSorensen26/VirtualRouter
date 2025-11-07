let SessionLoad = 1
let s:so_save = &g:so | let s:siso_save = &g:siso | setg so=0 siso=0 | setl so=-1 siso=-1
let v:this_session=expand("<sfile>:p")
silent only
silent tabonly
cd ~/VirtualRouter/VirtualRouter/src/protocols/neweigrp
if expand('%') == '' && !&modified && line('$') <= 1 && getline(1) == ''
  let s:wipebuf = bufnr('%')
endif
let s:shortmess_save = &shortmess
if &shortmess =~ 'A'
  set shortmess=aoOA
else
  set shortmess=aoO
endif
badd +214 core/InterfaceManager.cpp
badd +29 core/InterfaceManager.h
badd +132 interface/InterfaceTimers.cpp
badd +211 rtp/ReliableTransport.cpp
badd +30 rtp/NeighborTable.h
badd +17 rtp/ReliableRX.cpp
badd +1 ~/VirtualRouter/VirtualRouter/src/protocols/neweigrp/core/EigrpCore.h
badd +1 ~/VirtualRouter/VirtualRouter/src/protocols/neweigrp/core/EigrpCore.cpp
badd +14 ~/VirtualRouter/VirtualRouter/src/protocols/neweigrp/core/EigrpConfig.cpp
badd +36 ~/VirtualRouter/VirtualRouter/src/protocols/neweigrp/core/EigrpConfig.h
badd +184 ~/VirtualRouter/VirtualRouter/src/protocols/neweigrp/EigrpTypes.hpp
badd +97 rtp/TLVBuilder.cpp
badd +235 ~/VirtualRouter/VirtualRouter/src/protocols/neweigrp/topology/DuelEngine.cpp
badd +53 ~/VirtualRouter/VirtualRouter/src/protocols/neweigrp/topology/TopologyTable.cpp
argglobal
%argdel
edit ~/VirtualRouter/VirtualRouter/src/protocols/neweigrp/EigrpTypes.hpp
let s:save_splitbelow = &splitbelow
let s:save_splitright = &splitright
set splitbelow splitright
wincmd _ | wincmd |
vsplit
1wincmd h
wincmd w
let &splitbelow = s:save_splitbelow
let &splitright = s:save_splitright
wincmd t
let s:save_winminheight = &winminheight
let s:save_winminwidth = &winminwidth
set winminheight=0
set winheight=1
set winminwidth=0
set winwidth=1
exe 'vert 1resize ' . ((&columns * 30 + 106) / 213)
exe 'vert 2resize ' . ((&columns * 182 + 106) / 213)
argglobal
enew
file neo-tree\ filesystem\ \[1]
balt rtp/ReliableRX.cpp
setlocal foldmethod=expr
setlocal foldexpr=0
setlocal foldmarker={{{,}}}
setlocal foldignore=#
setlocal foldlevel=99
setlocal foldminlines=1
setlocal foldnestmax=20
setlocal foldenable
wincmd w
argglobal
balt ~/VirtualRouter/VirtualRouter/src/protocols/neweigrp/topology/DuelEngine.cpp
setlocal foldmethod=expr
setlocal foldexpr=DoxygenFold(v:lnum)
setlocal foldmarker={{{,}}}
setlocal foldignore=#
setlocal foldlevel=0
setlocal foldminlines=1
setlocal foldnestmax=20
setlocal foldenable
let s:l = 237 - ((52 * winheight(0) + 29) / 59)
if s:l < 1 | let s:l = 1 | endif
keepjumps exe s:l
normal! zt
keepjumps 237
normal! 047|
wincmd w
2wincmd w
exe 'vert 1resize ' . ((&columns * 30 + 106) / 213)
exe 'vert 2resize ' . ((&columns * 182 + 106) / 213)
tabnext 1
if exists('s:wipebuf') && len(win_findbuf(s:wipebuf)) == 0 && getbufvar(s:wipebuf, '&buftype') isnot# 'terminal'
  silent exe 'bwipe ' . s:wipebuf
endif
unlet! s:wipebuf
set winheight=1 winwidth=20
let &shortmess = s:shortmess_save
let &winminheight = s:save_winminheight
let &winminwidth = s:save_winminwidth
let s:sx = expand("<sfile>:p:r")."x.vim"
if filereadable(s:sx)
  exe "source " . fnameescape(s:sx)
endif
let &g:so = s:so_save | let &g:siso = s:siso_save
set hlsearch
nohlsearch
doautoall SessionLoadPost
unlet SessionLoad
" vim: set ft=vim :
