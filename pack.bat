for /f "tokens=5" %%i in ('SubWCRev  ..\^|find "Last committed at revision"') do set svnVersion=%%i
echo %svnVersion%
set buildDate=%date:~0,4%%date:~5,2%%date:~8,2%
echo %buildDate%

set buildTag=TDS_%buildDate%_%svnVersion%
echo %buildTag%

set buildTag1=TDS_%buildDate%
echo %buildTag1%

7za a -t7z %buildTag1%.zip ./out/tds.exe ./out/app -xr!.svn -xr!db -xr!log -xr!files -xr!unpackage -xr!tds.exp -xr!tds.lib -xr!tds.pdb -xr!apkPublish -xr!tds.ilk -xr!tds.ini