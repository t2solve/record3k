# BUILD: docker build --network=host -t recorder-3k .
# LOGIN: docker run  docker run --gpus all  --network=host  recorder-3k -it --name recorder-3k recorder-3k bash
# RUN: docker run  --gpus all  --network=host  recorder-3k
# podman run --rm -it --name recorder-3k \
#  --network=host -v "$PWD":/app -w /app \
# needed: export LD_LIBRARY_PATH=/app/bin:$LD_LIBRARY_PATH
#  recorder-3k bash   # or sh
# copy out data: docker cp dreamy_grothendieck:/app/out  .
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

# ONLY for debug 
#RUN apt-get update && apt-get install -y gdb

#build test 
RUN cd /app/ && qmake6 testrecord3k.pro
RUN cd /app/ && make -j 6

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

# Run the application
CMD ["/app/bin/test2"] 