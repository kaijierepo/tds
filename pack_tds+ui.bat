del .\out\ui\banner.svg
del .\out\ui\logo.svg
del .\out\ui\info.json

for /F %%i in ('svn info --show-item  revision') do set svnVersion=%%i
echo %svnVersion%
set buildDate=%date:~0,4%%date:~5,2%%date:~8,2%
echo %buildDate%

set buildTag=TDS_V1.0.%svnVersion%_%buildDate%
echo %buildTag%

7za a -t7z %buildTag%.zip ./out/tds.exe ./out/ui -xr!.svn -xr!db -xr!log -xr!files -xr!unpackage -xr!tds.exp -xr!tds.lib -xr!tds.pdb -xr!apkPublish -xr!tds.ilk -xr!tds.ini -xr!ioSimu.exe