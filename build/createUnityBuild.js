var fs = require('fs');
var path = require('path');
var rootPath = path.resolve('./src');
console.log(rootPath);
var allCpp = [];
getAllCpp(rootPath);
rootPath = path.resolve("./tdspro");
getAllCpp(rootPath);
function getAllCpp(filePath){
    var files = fs.readdirSync(filePath);
    for(var i=0;i<files.length;i++)
    {
        var filename = files[i];
        var filedir = path.join(filePath,filename);
        var stats = fs.statSync(filedir);
        if(stats.isFile()){
            var cppPath = filedir.replace(rootPath,".");
            if(cppPath.indexOf("3rdparty") >0)
            continue;
            if(cppPath.indexOf("unityBuild.cpp") >0)
            continue;
            if(cppPath.indexOf("main.cpp") > 0)
            continue;
            if(cppPath.indexOf("video") > 0)
            continue;
            if(cppPath.indexOf("not_used") > 0)
            continue;
            if(cppPath.indexOf("res") > 0)
            continue;
            if(cppPath.indexOf("tools") > 0)
            continue;
            if(cppPath.indexOf("tdsLicensing") > 0)
            continue;
            if(cppPath.indexOf("x64") > 0)
            continue;
            if(cppPath.indexOf("out") > 0)
            continue;
            if(cppPath.indexOf(".cpp") > 0)
            {
                allCpp.push(cppPath);
            }  
            else if(cppPath.indexOf(".c") > 0)
            {
                allCpp.push(cppPath);
            }  
        }
        if(stats.isDirectory()){
            if(filedir.indexOf("3rdparty") >0)
            continue;
            if(filedir.indexOf("build") >0)
            continue;
            if(filedir.indexOf(".vs") >0)
            continue;
            getAllCpp(filedir);
        }
    }
}

var outText = ""
for(var i=0;i<allCpp.length;i++)
{
    var oneLine = "#include \""+ allCpp[i] + "\"" +"\r\n";
    outText += oneLine;
}

console.log("file num:" + allCpp.length);
fs.writeFileSync("./build/unityBuild.cpp",outText,"utf8");