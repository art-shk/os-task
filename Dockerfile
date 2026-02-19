FROM alpine:3.18

WORKDIR /app

RUN apk add --no-cache gcc make musl-dev

COPY . .

CMD ["/bin/sh"]