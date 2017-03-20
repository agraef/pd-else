ELSE - EL Locus Solus' Else - library for Pd

"EL Locus Solus" is run by Alexandre Torres Porres, who organizes cultural events and teaches computer music courses since around 2009, at the time of PdCon09 (the 3rd International Pure Data Convention in 2009, São Paulo - Brasil); website: http://alexandre-torres.wixsite.com/el-locus-solus

EL Locus Solus offers a computer music tutorial with examples in Pure Data for its courses. These are in portuguese as of yet, with plans for english translation and formatting into a book. There are over 350 examples for now and they were designed to work with Pd Extended 0.42-5, making extensive use of the existing objects available in Pd Extended's libraries.

Even though extended has quite a large set of external libraries and objects, at some point there was the need of something "else". Thus, EL Locus Solus is now not only offering computer music examples, but also the "else" library, with extra objects for its didactic material.

----------------

The current library state is at alpha experimental releases, where drastic changes may occur and and backwards compatibility is not guaranteed for future releases

latest release: 1.0-alpha2, from march 20th 2017 => https://github.com/porres/pd-else/releases

Porres; 2017

----------------------

Objects:

OSCILLATORS (7):
- [imp~]
- [imp2~]
- [par~]
- [pulse~]
- [sawtooth~]
- [square~]
- [vartri~]

CONVERSION (12):
- [cents2rato]
- [cents2ratio~]
- [f2s~]
- [hz2rad]
- [hz2rad~]
- [rad2hz]
- [rad2hz~]
- [ratio2cents]
- [ratio2cents~]
- [rescale]
- [rescale~]
- [s2f~]

SIGNAL ANALYSIS (7):
- [changed~]
- [elapsed~]
- [lastvalue~]
- [median~]
- [togedge~]
- [trigcount~]
- [zerocross~]

TRIGGERS (5):
- [dust~]
- [pimp~]
- [sh~]
- [toggleff~]
- [trigate~]

CONSTANT VALUES (3):
- [nyquist]
- [pi]
- [sr~]

MISCELANEOUS (6):
- [accum~]
- [downsample~]
- [lastvalue]
- [lfnoise~]
- [out~]
- [sin~]
