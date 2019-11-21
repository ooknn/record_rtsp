
INCLUDE_DIR=-I ${HOME}/tools/install/ffmpeg/include \
			-I${HOME}/tools/live555/UsageEnvironment/include \
			-I${HOME}/tools/live555/groupsock/include \
			-I${HOME}/tools/live555/BasicUsageEnvironment/include  \
			-I${HOME}/tools/live555/liveMedia/include 
			
LIB_DIR= -L ${HOME}/tools/install/ffmpeg/lib -L/usr/local/lib \
		 -L${HOME}/tools/live555/liveMedia \
		 -L${HOME}/tools/live555/groupsock \
		 -L${HOME}/tools/live555/BasicUsageEnvironment \
		 -L${HOME}/tools/live555/UsageEnvironment


LIBS=-lavdevice -lavfilter -lavformat -lavcodec \
-lavresample -lpostproc -lswresample -lswscale \
-lavutil -lxcb -lxcb-shm -lxcb-shape -lxcb-xfixes \
-lasound -lSDL2 -pthread  -lass  -lvidstab -lm -lgomp \
-lpthread -lfreetype  -lbz2 -lz  -lvpx  -llzma -lopencore-amrwb \
-laom -lfdk-aac -lmp3lame -lopencore-amrnb -lopenjp2 \
-lopus -ltheoraenc -ltheoradec -logg -lvorbis -lvorbisenc \
-lx264  -lxvidcore  -lkvazaar  -pthread  -ldl -lpthread  -lX11 -lliveMedia -lgroupsock -lUsageEnvironment -lBasicUsageEnvironment

FLAG= -std=c++17   -g -Wl,-rpath,/usr/local/lib -Wl,--enable-new-dtags
CC=clang++ 
SOURCE= codec.cc  frame_source.cc  main.cc  rtsp_server.cc  sub_session.cc

app:
	${CC} ${FLAG} ${SOURCE} ${INCLUDE_DIR} ${LIB_DIR} ${LIBS} -static-libstdc++  -Wl,-Bstatic -lx265  -lnuma -lssl -lcrypto  -o record #-Wl,-Bdynamic -ltcmalloc


