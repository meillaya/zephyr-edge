# Multi-stage build for the cloud telemetry ingestion service.
FROM golang:1.24-alpine AS builder
WORKDIR /app
COPY go.mod go.sum ./
RUN go mod download
COPY . .
RUN CGO_ENABLED=0 go build -o /cloud-ingest .

FROM alpine:3.21
RUN apk add --no-cache ca-certificates \
    && addgroup -S -g 10001 app \
    && adduser -S -u 10001 -G app app
COPY --from=builder /cloud-ingest /usr/local/bin/cloud-ingest
USER app
EXPOSE 9090
ENTRYPOINT ["cloud-ingest"]
