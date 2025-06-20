var exec = require('child_process').exec;
var fs = require("fs");
try{
    fs.unlinkSync("./build/build.txt")
}
catch{

}

var startTime = new Date();

var child = exec("call ./build/build.bat");

child.stdout.on('data', function(data) {
    console.log(data);
    fs.appendFileSync("./build/build.txt",data,"utf8");
});
child.on('close', function() {
    var endTime = new Date();
    var cost = (endTime - startTime)/1000.0;
    console.log('编译结束,耗时' + cost + '秒');
});