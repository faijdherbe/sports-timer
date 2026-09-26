FROM python:3.13-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      nodejs npm libsdl2-2.0-0 libglib2.0-0 libpixman-1-0 zlib1g libsndio7.0 libpng16-16t64 git curl \
    && rm -rf /var/lib/apt/lists/*
RUN pip install --no-cache-dir pebble-tool
# World-writable SDK home so the container can run as the host user (no root-owned build/).
ENV HOME=/pebble
RUN mkdir /pebble && yes | pebble sdk install latest && chmod -R a+rwX /pebble
# Login tokens live in /pebble/auth; pebble.sh mounts a host dir there so `pebble login` persists.
RUN mkdir -m 777 /pebble/auth && cd /pebble/.local/share/pebble-sdk \
    && rm -rf oauth_firebase && ln -s /pebble/auth oauth_firebase
WORKDIR /app
CMD ["pebble", "build"]
