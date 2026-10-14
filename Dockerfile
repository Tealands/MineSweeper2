FROM debian:bookworm-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY Minesweeper2.cpp .
RUN mkdir -p /app \
    && g++ -std=c++20 -O2 -Wall -Wextra -pedantic -pthread -o /app/minesweeper Minesweeper2.cpp

FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/minesweeper .
COPY field.txt .

CMD ["/app/minesweeper", "--web"]
