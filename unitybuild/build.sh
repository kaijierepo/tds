set -e

cd ../src
pwd

common_flags="\
-g \
-DENABLE_ALM_SRV_HOOK_SCRIPT \
-DENABLE_QJS \
-DENABLE_QJS_HTTP \
-DUTF8 \
-DUSE_SVN_REV \
-DTDS \
-DENABLE_JERRY_SCRIPT \
-D_TDS \
-DMG_TLS=MG_TLS_BUILTIN \
-DMG_ENABLE_POLL \
-D_HAS_STD_BYTE=0 \
-DCONF_FILE \
-D_POSIX_C_SOURCE=200809L \
-D_GNU_SOURCE
-I ./ \
-I ./include \
-I ./script \
-I ./common \
-I ./io_server \
-I ./data_server \
-I ./func_module \
-I ./mongoose \
-fPIC \
-pthread \
"

c_flags="\
-std=gnu99 \
"

cpp_flags="\
-std=gnu++17 \
-fpermissive \
"

linkerflags="-lpthread -lcrypto -lkrb5 -lssl -lutil -lrt -latomic"

gcc $common_flags $c_flags -c ./common/base64.c -o ./common/base64.o 
gcc $common_flags $c_flags -c ./common/miniz.c -o ./common/miniz.o
gcc $common_flags $c_flags -c ./common/yyjson.c -o ./common/yyjson.o
gcc $common_flags $c_flags -c ./mongoose/mongoose.c -o ./mongoose/mongoose.o
gcc $common_flags $c_flags -c ./script/unicode_data.c -o ./script/unicode_data.o
gcc $common_flags $c_flags -c ./script/cutils.c -o ./script/cutils.o
gcc $common_flags $c_flags -c ./script/dtoa.c -o ./script/dtoa.o
gcc $common_flags $c_flags -c ./script/libregexp.c -o ./script/libregexp.o
gcc $common_flags $c_flags -c ./script/libunicode.c -o ./script/libunicode.o
gcc $common_flags $c_flags -c ./script/quickjs-libc.c -o ./script/quickjs-libc.o
gcc $common_flags $c_flags -c ./script/quickjs.c -o ./script/quickjs.o
gcc $common_flags $c_flags -c ./script/repl.c -o ./script/repl.o

g++ $common_flags $cpp_flags -c ./CDataSimu.cpp -o ./CDataSimu.o
g++ $common_flags $cpp_flags -c ./main.cpp -o ./main.o
g++ $common_flags $cpp_flags -c ./pch.cpp -o ./pch.o
g++ $common_flags $cpp_flags -c ./tds_imp.cpp -o ./tds_imp.o
g++ $common_flags $cpp_flags -c ./test.cpp -o ./test.o
g++ $common_flags $cpp_flags -c ./common/common.cpp -o ./common/common.o
g++ $common_flags $cpp_flags -c ./common/dtwrecoge.cpp -o ./common/dtwrecoge.o
g++ $common_flags $cpp_flags -c ./common/kvIni.cpp -o ./common/kvIni.o
g++ $common_flags $cpp_flags -c ./common/logger.cpp -o ./common/logger.o
g++ $common_flags $cpp_flags -c ./common/md5.cpp -o ./common/md5.o
g++ $common_flags $cpp_flags -c ./common/memDiag.cpp -o ./common/memDiag.o
g++ $common_flags $cpp_flags -c ./common/secure.cpp -o ./common/secure.o
g++ $common_flags $cpp_flags -c ./common/sha1.cpp -o ./common/sha1.o
g++ $common_flags $cpp_flags -c ./common/sha256.cpp -o ./common/sha256.o
g++ $common_flags $cpp_flags -c ./common/stream2pkt.cpp -o ./common/stream2pkt.o
g++ $common_flags $cpp_flags -c ./common/tcpClt.cpp -o ./common/tcpClt.o
g++ $common_flags $cpp_flags -c ./common/tcpSrv.cpp -o ./common/tcpSrv.o
g++ $common_flags $cpp_flags -c ./common/udpSrv.cpp -o ./common/udpSrv.o
g++ $common_flags $cpp_flags -c ./data_server/as_interface.cpp -o ./data_server/as_interface.o
g++ $common_flags $cpp_flags -c ./data_server/mp.cpp -o ./data_server/mp.o
g++ $common_flags $cpp_flags -c ./data_server/mqttSrv.cpp -o ./data_server/mqttSrv.o
g++ $common_flags $cpp_flags -c ./data_server/obj.cpp -o ./data_server/obj.o
g++ $common_flags $cpp_flags -c ./data_server/prj.cpp -o ./data_server/prj.o
g++ $common_flags $cpp_flags -c ./data_server/rpcHandler.cpp -o ./data_server/rpcHandler.o
g++ $common_flags $cpp_flags -c ./data_server/rpcHandler_common.cpp -o ./data_server/rpcHandler_common.o
g++ $common_flags $cpp_flags -c ./data_server/scriptEngine.cpp -o ./data_server/scriptEngine.o
g++ $common_flags $cpp_flags -c ./data_server/scriptFunc.cpp -o ./data_server/scriptFunc.o
g++ $common_flags $cpp_flags -c ./data_server/scriptManager.cpp -o ./data_server/scriptManager.o
g++ $common_flags $cpp_flags -c ./data_server/tAlmSrv.cpp -o ./data_server/tAlmSrv.o
g++ $common_flags $cpp_flags -c ./data_server/tdb.cpp -o ./data_server/tdb.o
g++ $common_flags $cpp_flags -c ./data_server/tdsSession.cpp -o ./data_server/tdsSession.o
g++ $common_flags $cpp_flags -c ./data_server/tSockSrv.cpp -o ./data_server/tSockSrv.o
g++ $common_flags $cpp_flags -c ./data_server/webSrv.cpp -o ./data_server/webSrv.o
g++ $common_flags $cpp_flags -c ./func_module/csvTable.cpp -o ./func_module/csvTable.o
g++ $common_flags $cpp_flags -c ./func_module/dumpCatch.cpp -o ./func_module/dumpCatch.o
g++ $common_flags $cpp_flags -c ./func_module/fileUploadSrv.cpp -o ./func_module/fileUploadSrv.o
g++ $common_flags $cpp_flags -c ./func_module/logServer.cpp -o ./func_module/logServer.o
g++ $common_flags $cpp_flags -c ./func_module/statusServer.cpp -o ./func_module/statusServer.o
g++ $common_flags $cpp_flags -c ./func_module/taskServer.cpp -o ./func_module/taskServer.o
g++ $common_flags $cpp_flags -c ./func_module/tdsConf.cpp -o ./func_module/tdsConf.o
g++ $common_flags $cpp_flags -c ./func_module/userMng.cpp -o ./func_module/userMng.o
g++ $common_flags $cpp_flags -c ./include/tds.cpp -o ./include/tds.o
g++ $common_flags $cpp_flags -c ./io_server/ioChan.cpp -o ./io_server/ioChan.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev.cpp -o ./io_server/ioDev.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_bacnet.cpp -o ./io_server/ioDev_bacnet.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_custom.cpp -o ./io_server/ioDev_custom.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_dcqk.cpp -o ./io_server/ioDev_dcqk.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_dlt645_2007.cpp -o ./io_server/ioDev_dlt645_2007.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_eip.cpp -o ./io_server/ioDev_eip.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_iq60.cpp -o ./io_server/ioDev_iq60.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_modbusRtu.cpp -o ./io_server/ioDev_modbusRtu.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_modbusSlave.cpp -o ./io_server/ioDev_modbusSlave.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_modbusTcp.cpp -o ./io_server/ioDev_modbusTcp.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_mqtt.cpp -o ./io_server/ioDev_mqtt.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_onvif.cpp -o ./io_server/ioDev_onvif.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_srvStatus.cpp -o ./io_server/ioDev_srvStatus.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_tdsp.cpp -o ./io_server/ioDev_tdsp.o
g++ $common_flags $cpp_flags -c ./io_server/ioDev_visca.cpp -o ./io_server/ioDev_visca.o
g++ $common_flags $cpp_flags -c ./io_server/ioGW_localSerial.cpp -o ./io_server/ioGW_localSerial.o
g++ $common_flags $cpp_flags -c ./io_server/ioGW_rs485ToNet.cpp -o ./io_server/ioGW_rs485ToNet.o
g++ $common_flags $cpp_flags -c ./io_server/ioSrv.cpp -o ./io_server/ioSrv.o
g++ $common_flags $cpp_flags -c ./io_server/proto_common.cpp -o ./io_server/proto_common.o
g++ $common_flags $cpp_flags -c ./io_server/proto_eip.cpp -o ./io_server/proto_eip.o
g++ $common_flags $cpp_flags -c ./io_server/proto_tb3386.cpp -o ./io_server/proto_tb3386.o
g++ $common_flags $cpp_flags -c ./io_server/proto_ws.cpp -o ./io_server/proto_ws.o


obj_files="\
./common/base64.o \
./common/miniz.o \
./common/yyjson.o \
./mongoose/mongoose.o \
./script/unicode_data.o \
./script/cutils.o \
./script/dtoa.o \
./script/libregexp.o \
./script/libunicode.o \
./script/quickjs-libc.o \
./script/quickjs.o \
./script/repl.o \

./CDataSimu.o \
./main.o \
./pch.o \
./tds_imp.o \
./test.o \
./common/common.o \
./common/dtwrecoge.o \
./common/kvIni.o \
./common/logger.o \
./common/md5.o \
./common/memDiag.o \
./common/secure.o \
./common/sha1.o \
./common/sha256.o \
./common/stream2pkt.o \
./common/tcpClt.o \
./common/tcpSrv.o \
./common/udpSrv.o \
./data_server/as_interface.o \
./data_server/mp.o \
./data_server/mqttSrv.o \
./data_server/obj.o \
./data_server/prj.o \
./data_server/rpcHandler.o \
./data_server/rpcHandler_common.o \
./data_server/scriptEngine.o \
./data_server/scriptFunc.o \
./data_server/scriptManager.o \
./data_server/tAlmSrv.o \
./data_server/tdb.o \
./data_server/tdsSession.o \
./data_server/tSockSrv.o \
./data_server/webSrv.o \
./func_module/csvTable.o \
./func_module/dumpCatch.o \
./func_module/fileUploadSrv.o \
./func_module/logServer.o \
./func_module/statusServer.o \
./func_module/taskServer.o \
./func_module/tdsConf.o \
./func_module/userMng.o \
./include/tds.o \
./io_server/ioChan.o \
./io_server/ioDev.o \
./io_server/ioDev_bacnet.o \
./io_server/ioDev_custom.o \
./io_server/ioDev_dcqk.o \
./io_server/ioDev_dlt645_2007.o \
./io_server/ioDev_eip.o \
./io_server/ioDev_iq60.o \
./io_server/ioDev_modbusRtu.o \
./io_server/ioDev_modbusSlave.o \
./io_server/ioDev_modbusTcp.o \
./io_server/ioDev_mqtt.o \
./io_server/ioDev_onvif.o \
./io_server/ioDev_srvStatus.o \
./io_server/ioDev_tdsp.o \
./io_server/ioDev_visca.o \
./io_server/ioGW_localSerial.o \
./io_server/ioGW_rs485ToNet.o \
./io_server/ioSrv.o \
./io_server/proto_common.o \
./io_server/proto_eip.o \
./io_server/proto_tb3386.o \
./io_server/proto_ws.o \
"

g++ $common_flags $cpp_flags $obj_files -o ../out/tds/tds $linkerflags