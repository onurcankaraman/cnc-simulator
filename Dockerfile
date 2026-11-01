FROM debian:bookworm-slim@sha256:abd67ffcfa541b485a3dff59865ab629aa048a6c613e639d36e7456b0b229241 AS dependencies
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake ninja-build git ca-certificates python3 libsqlite3-dev=3.40.1-2+deb12u2 libmosquitto-dev=2.0.11-1.2+deb12u2 libyaml-cpp-dev=0.7.0+dfsg-8+b1 nlohmann-json3-dev=3.11.2-2 libssl-dev=3.0.22-1~deb12u2 curl && rm -rf /var/lib/apt/lists/*
WORKDIR /deps
RUN git clone https://github.com/SCADACS/snap7.git && cd snap7 && git checkout f6ff90317ca5d54250f4dcd29209689a74e26d82 && g++ -O2 -fPIC -shared -pthread -std=c++11 -Isrc/sys -Isrc/core -Isrc/lib src/sys/*.cpp src/core/*.cpp src/lib/*.cpp -o /usr/local/lib/libsnap7.so && cp release/Wrappers/c-cpp/snap7.h /usr/local/include/ && ldconfig
RUN git clone --branch v1.5.0 --depth 1 https://github.com/open62541/open62541.git && cd open62541 && test "$(git rev-parse HEAD)" = b90fbfac5ef484089447bf5e3fe6ad7baaaebada && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DUA_ENABLE_ENCRYPTION=OPENSSL -DUA_ENABLE_DISCOVERY=OFF -DUA_BUILD_EXAMPLES=OFF -DUA_BUILD_UNIT_TESTS=OFF && cmake --build build -j2 && cmake --install build && ldconfig
FROM dependencies AS build
WORKDIR /app
COPY . .
RUN python3 scripts/generate-contract.py && cmake -S . -B build -G Ninja && cmake --build build -j2 && ctest --test-dir build --output-on-failure && strip build/cnc-simulator
FROM debian:bookworm-slim@sha256:abd67ffcfa541b485a3dff59865ab629aa048a6c613e639d36e7456b0b229241 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends libsqlite3-0=3.40.1-2+deb12u2 libmosquitto1=2.0.11-1.2+deb12u2 libyaml-cpp0.7=0.7.0+dfsg-8+b1 libssl3=3.0.22-1~deb12u2 curl ca-certificates && rm -rf /var/lib/apt/lists/* && useradd -u 10001 -m cnc && mkdir /data && chown cnc /data
COPY --from=build /usr/local/lib/libsnap7.so /usr/local/lib/
COPY --from=build /usr/local/lib/libopen62541.so* /usr/local/lib/
RUN ldconfig
WORKDIR /app
COPY --from=build /app/build/cnc-simulator /app/cnc-simulator
COPY config ./config
COPY src/ui ./ui
RUN chmod -R a+rX /app/config /app/ui
USER cnc
ENTRYPOINT ["/app/cnc-simulator"]
