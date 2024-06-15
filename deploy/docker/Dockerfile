FROM    127.0.0.1:5000/homed-service:latest
ARG     TARGETARCH
RUN     mkdir /etc/homed
RUN     ln -s /data/homed-recorder.conf /etc/homed/homed-recorder.conf
RUN     ln -s /data/homed.log /var/log/homed.log
COPY    homed-recorder-$TARGETARCH /usr/bin/homed-recorder
ENV     QT_PLUGIN_PATH=/usr/lib/qt5/plugins
CMD     ["/usr/bin/homed-recorder"]
