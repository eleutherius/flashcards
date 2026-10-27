# Збірка на macOS/Windows: тулчейн PocketBook — це x86-64 Linux-бінарники.
#   docker build --platform linux/amd64 -t pb-flashcards .
#   docker run --rm --platform linux/amd64 -v "$PWD":/src pb-flashcards package
FROM debian:12-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends git ca-certificates make python3 zip \
 && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 --branch 6.5 --filter=blob:none --sparse \
        https://github.com/pocketbook/SDK_6.3.0 /opt/pb \
 && cd /opt/pb && git sparse-checkout set SDK-B288
WORKDIR /src
ENTRYPOINT ["make", "SDK=/opt/pb/SDK-B288"]
CMD ["all"]
