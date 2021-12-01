var mpB_Level = getMo("B.水位");
var mpA_Pump = getMo("A.开关");
var mpC_Level = getMo("C.水位");
var mpB_Pump = getMo("B.开关");

function isValid(val)
{
    if(val == "?" || val=="-")
        return false;
    return true;
}

if(mpB_Level != null &&
   mpA_Pump != null &&
   mpC_Level != null &&
   mpB_Pump != null )
{
    if(isValid(mpB_Level.val) && 
    isValid(mpA_Pump.val) && 
    isValid(mpC_Level.val) && 
    isValid(mpB_Pump.val))
    {
        //调试用日志
        //log("B水位=" + mpB_Level.val + ",A水泵=" +  mpA_Pump.val);
        //log("C水位=" + mpC_Level.val + ",B水泵=" +  mpB_Pump.val);
        
        //B点水位控制A点加水
        //水位大于10米,关闭水泵
        if(mpB_Level.val > 10 && mpA_Pump.val == true)
        {
            output("A.开关",false);
            log("B点水位大于10m,关闭A点水泵");
        }
        //水位小于2米,打开水泵
        if(mpB_Level.val < 10 && mpA_Pump.val == false)
        {
            output("A.开关",true);
            log("B点水位小于2m,打开A点水泵");
        }

        //C点水位控制B点加水
        if(mpC_Level.val > 10 && mpB_Pump.val == true)
        {
            output("B.开关",false);
            log("C点水位大于10m,关闭B点水泵");
        }
        //水位小于2米,打开水泵
        if(mpC_Level.val < 10 && mpB_Pump.val == false)
        {
            output("B.开关",true);
            log("C点水位小于2m,打开B点水泵");
        }
    }
}
else
{
    log("控制脚本 水泵控制.js 未能获取到所有位号,请检查组态!");
}