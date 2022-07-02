FROM ubuntu

LABEL maintainer="Yutaro Hayakawa <yutaro.hayakawa@linecorp.com>"

RUN apt-get update && apt-get -y install git init

WORKDIR /root
RUN git clone https://github.com/YutaroHayakawa/Prism-HTTP
RUN bash Prism-HTTP/install_deps.sh

WORKDIR /root/Prism-HTTP
RUN make -C switch lib/libprism-switch-client.a
RUN make -C src all
