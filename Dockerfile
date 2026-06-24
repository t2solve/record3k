# BUILD: podman build --network=host -t recorder-3k .
# RUN: podman run --rm -it --name recorder-3k --network=host recorder-3k bash
# podman run --rm -it --name recorder-3k --gpus nvidia.com/all --network=host recorder-3k bash
# DEBUG (with ping / ICMP enabled — NET_RAW cap + disable seccomp raw-socket filter):
# podman run --rm -it --name recorder-3k --gpus nvidia.com/all --network=host --cap-add NET_RAW --security-opt seccomp=unconfined recorder-3k bash
# 

FROM cuda-builder:latest 

# Set environment variables to avoid timezone prompts
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# Set the working directory
WORKDIR /app

# Create necessary directories
RUN mkdir -p /app/bin /app/lib

# copy is not working copy from local
COPY . .

# ensure public include prefix exists; move flat headers if present
RUN if [ ! -d /app/src/liblbt/include/liblbt ]; then \
      mkdir -p /app/src/liblbt/include/liblbt; \
      for f in /app/src/liblbt/include/*.h; do \
        [ -e "$f" ] && mv "$f" /app/src/liblbt/include/liblbt/; \
      done; \
    fi

# sanity check
RUN ls -al /app/src/liblbt/include/liblbt

# Debug tools (gdb for crash diagnosis, gdbserver for remote debugging)
RUN apt-get update && apt-get install -y --no-install-recommends gdb gdbserver libsqlite3-dev

# Python3 + ping for camera debug scripting (debug_camera_network.py)
RUN apt-get update && apt-get install -y --no-install-recommends python3 iputils-ping 

# Ensure qmake's PKGCONFIG += opencv4 can find opencv4.pc regardless of base image age
ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

#build test with production log level (INFO)
# For debug builds, add: LOG_LEVEL=DEBUG
RUN pkg-config --modversion opencv4 || (echo "ERROR: opencv4 not found via pkg-config — PKG_CONFIG_PATH=$PKG_CONFIG_PATH" && exit 1)
RUN cd /app/ && qmake6 CONFIG+=cuda CONFIG+=api CONFIG+=vimbax LOG_LEVEL=INFO record3k.pro
RUN cd /app/ && make clean && make -j 6

# install the Vimba SDK drivers 
RUN /app/vimbax/cti/Install_GenTL_Path.sh
RUN /app/vimbax/cti/Set_GenTL_Path.sh

# Determine the correct GenTL path and set the environment variable
RUN export CWD="/app/vimbax/cti" && \
    export UNAME=$(uname -m) && \
    if [ "$UNAME" = "aarch64" ]; then ARCH=arm; \
    elif [ "$UNAME" = "amd64" ] || [ "$UNAME" = "x86_64" ]; then ARCH=x86; \
    else echo "Error: Incompatible system architecture found." 1>&2; exit 1; fi && \
    echo "Setting GENICAM_GENTL64_PATH to :$CWD" && \
    echo "export GENICAM_GENTL64_PATH=:$CWD" >> /etc/profile.d/gentlpath.sh

ENV GENICAM_GENTL64_PATH=:/app/vimbax/cti

#copy need vimba cti file
RUN cp  -r /app/vimbax/cti/VimbaGigETL.cti  /app/bin/


# add vimba lib path to LD_LIBRARY_PATH
ENV LD_LIBRARY_PATH=/app/vimbax/api/lib/:$LD_LIBRARY_PATH

# add own lib path to LD_LIBRARY_PATH
ENV LD_LIBRARY_PATH=/app/lib:$LD_LIBRARY_PATH

# copy default config to bin
RUN cp  -r /app/config/examples/intialrun/*  /app/bin/

# list bin content
RUN ls -al /app/bin

#copy need vimba cti file
RUN cp /app/entrypoint.sh /app/bin/ && chmod +x /app/bin/entrypoint.sh

# Run the application from its own directory (required for relative path lookups)
WORKDIR /app/bin
CMD ["./entrypoint.sh", "./apiserver"]