# BUILD: docker build --network=host -t recorder-3k .
# LOGIN: docker run --network=host  -it --name recorder-3k recorder-3k bash
# RUN: docker run --network=host  recorder-3k
# copy out data: docker cp dreamy_grothendieck:/app/out  .
FROM cuda-builder:latest 

# Set environment variables to avoid timezone prompts
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# Set the working directory
WORKDIR /app

# copy is not working copy from local
COPY . .

# ONLY for debug 
#RUN apt-get update && apt-get install -y gdb

#build test 
RUN cd /app/obj && qmake6 recorder-test.pro
RUN cd /app/obj && make -j 6

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
CMD ["/app/bin/test"] 