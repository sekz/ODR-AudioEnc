#!/bin/bash
# ODR-AudioEnc Multi-Instance Entrypoint Script
# Manages multiple audioenc instances in a single container
# POSIX compliant for maximum compatibility

set -e

# Configuration from environment variables
INSTANCE_COUNT="${INSTANCE_COUNT:-1}"
FIFO_BASE_PATH="${FIFO_BASE_PATH:-/tmp/fifo}"
PORT_BASE="${PORT_BASE:-8010}"
API_PORT_BASE="${API_PORT_BASE:-9200}"
LOG_DIR="${LOG_DIR:-/app/logs}"
BITRATE="${BITRATE:-128}"
SAMPLE_RATE="${SAMPLE_RATE:-48000}"
CHANNELS="${CHANNELS:-2}"

# Binary location (try multiple paths)
BINARY_PATHS=(
    "/app/src/odr-audioenc"
    "/app/build/bin/odr-audioenc"
    "/app/odr-audioenc"
    "/usr/local/bin/odr-audioenc"
)

BINARY=""
for path in "${BINARY_PATHS[@]}"; do
    if [ -x "$path" ]; then
        BINARY="$path"
        echo "✓ Found ODR-AudioEnc binary: $BINARY"
        break
    fi
done

if [ -z "$BINARY" ]; then
    echo "ERROR: odr-audioenc binary not found in: ${BINARY_PATHS[*]}"
    exit 1
fi

# Create required directories
mkdir -p "$FIFO_BASE_PATH"
mkdir -p "$LOG_DIR"
chmod 1777 "$FIFO_BASE_PATH"

# Array to store process PIDs
declare -a PIDS=()

# Cleanup function
cleanup() {
    echo "Cleaning up audioenc instances..."
    for pid in "${PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            echo "Stopping audioenc instance (PID: $pid)"
            kill -TERM "$pid" 2>/dev/null || true
        fi
    done

    # Clean up FIFOs
    rm -f "$FIFO_BASE_PATH"/audio*.fifo

    echo "Cleanup complete"
    exit 0
}

trap cleanup INT TERM

echo "========================================"
echo "ODR-AudioEnc Multi-Instance Manager"
echo "========================================"
echo "Instance Count: $INSTANCE_COUNT"
echo "FIFO Base Path: $FIFO_BASE_PATH"
echo "Port Range: $PORT_BASE-$((PORT_BASE + INSTANCE_COUNT - 1))"
echo "API Port Range: $API_PORT_BASE-$((API_PORT_BASE + INSTANCE_COUNT - 1))"
echo "Binary: $BINARY"
echo "Bitrate: ${BITRATE}k"
echo "Sample Rate: ${SAMPLE_RATE} Hz"
echo "Channels: $CHANNELS"
echo "========================================"

# Start each instance
for i in $(seq 1 "$INSTANCE_COUNT"); do
    INSTANCE_ID=$i
    FIFO_PATH="$FIFO_BASE_PATH/audio${INSTANCE_ID}.fifo"
    PORT=$((PORT_BASE + INSTANCE_ID - 1))
    API_PORT=$((API_PORT_BASE + INSTANCE_ID - 1))
    LOG_FILE="$LOG_DIR/audioenc-${INSTANCE_ID}.log"

    echo ""
    echo "Starting AudioEnc instance $INSTANCE_ID..."
    echo "  FIFO: $FIFO_PATH"
    echo "  Input Port: $PORT"
    echo "  API Port: $API_PORT"
    echo "  Log: $LOG_FILE"

    # Create FIFO for this instance
    rm -f "$FIFO_PATH"
    mkfifo "$FIFO_PATH"
    chmod 666 "$FIFO_PATH"

    # Start audioenc instance in background
    # Input: TCP listener on port
    # Output: FIFO for PAD insertion by padenc
    "$BINARY" \
        --input="tcp://*:$PORT" \
        --output="$FIFO_PATH" \
        --bitrate="$BITRATE" \
        --format=s16le \
        --channels="$CHANNELS" \
        --rate="$SAMPLE_RATE" \
        --drift-comp \
        --enable-dab-rc \
        > "$LOG_FILE" 2>&1 &

    INSTANCE_PID=$!
    PIDS+=("$INSTANCE_PID")

    echo "  Started with PID: $INSTANCE_PID"

    # Small delay between instances
    sleep 0.5
done

echo ""
echo "========================================"
echo "All $INSTANCE_COUNT AudioEnc instances started"
echo "PIDs: ${PIDS[*]}"
echo "========================================"
echo ""
echo "Monitoring instances (checking every 5 seconds)..."

# Monitor instances - restart if any die
RESTART_COUNT=0
MAX_RESTARTS=5

while true; do
    NEED_RESTART=false

    for i in "${!PIDS[@]}"; do
        pid="${PIDS[$i]}"
        instance_id=$((i + 1))

        if ! kill -0 "$pid" 2>/dev/null; then
            echo "WARNING: AudioEnc instance $instance_id (PID: $pid) died!"
            NEED_RESTART=true

            # Log the crash
            echo "$(date): Instance $instance_id crashed" >> "$LOG_DIR/crashes.log"
        fi
    done

    if [ "$NEED_RESTART" = true ]; then
        RESTART_COUNT=$((RESTART_COUNT + 1))
        echo "Instance failure detected (restart count: $RESTART_COUNT/$MAX_RESTARTS)"

        if [ $RESTART_COUNT -ge $MAX_RESTARTS ]; then
            echo "ERROR: Too many restarts ($RESTART_COUNT). Exiting for Docker to restart container."
            exit 1
        else
            echo "Container will exit for Docker to restart all instances."
            exit 1
        fi
    fi

    sleep 5
done
