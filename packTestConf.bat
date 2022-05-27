del .\out\ui\banner.svg
del .\out\ui\logo.svg
del .\out\ui\info.json

for /F %%i in ('svn info --show-item  revision') do set svnVersion=%%i
echo %svnVersion%
set buildDate=%date:~0,4%%date:~5,2%%date:~8,2%
echo %buildDate%

set buildTag=TDS_%buildDate%_%svnVersion%
echo %buildTag%

7za a -t7z %buildTag%.zip ./out/tds.exe ./out/ui ./out/ioSimu.exe ./out/conf -xr!.svn -xr!db -xr!log -xr!files -xr!unpackage -xr!tds.exp -xr!tds.lib -xr!tds.pdb -xr!apkPublish -xr!tds.ilk -xr!tds.ini