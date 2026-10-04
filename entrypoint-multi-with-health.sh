#!/bin/bash
# ODR-AudioEnc Multi-Instance Entrypoint Script with Health Endpoints
# StreamDAB EncoderManager - Phase 2.1 Enhanced
# Starts multiple AudioEnc instances with health monitoring

set -e

echo "========================================"
echo "ODR-AudioEnc Multi-Instance Container"
echo "========================================"

# Configuration from environment
INSTANCE_COUNT=${INSTANCE_COUNT:-3}
FIFO_BASE_PATH=${FIFO_BASE_PATH:-/tmp/fifo}
PORT_BASE=${PORT_BASE:-8010}
API_PORT_BASE=${API_PORT_BASE:-9200}
BITRATE=${BITRATE:-128}
CHANNELS=${CHANNELS:-2}
SAMPLE_RATE=${SAMPLE_RATE:-48000}
LOG_DIR=${LOG_DIR:-/var/log/odr}

# Binary location (try multiple paths)
BINARY_PATHS=(
    "/usr/bin/odr-audioenc"           # Mock binary location
    "/app/src/odr-audioenc"           # Volume-mounted development
    "/app/build/bin/odr-audioenc"     # Production build
    "/app/odr-audioenc"               # Alternative location
    "/usr/local/bin/odr-audioenc"     # System install
)

BINARY=""
for path in "${BINARY_PATHS[@]}"; do
    if [ -f "$path" ]; then
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
chmod 777 "$FIFO_BASE_PATH" 2>/dev/null || true
chmod 755 "$LOG_DIR" 2>/dev/null || true

# Arrays to track PIDs
PIDS=()
HEALTH_PIDS=()

echo ""
echo "Configuration:"
echo "========================================"
echo "Instance Count: $INSTANCE_COUNT"
echo "FIFO Base Path: $FIFO_BASE_PATH"
echo "Port Base: $PORT_BASE"
echo "API Port Base: $API_PORT_BASE"
echo "Bitrate: ${BITRATE}k"
echo "Channels: $CHANNELS"
echo "Sample Rate: ${SAMPLE_RATE}Hz"
echo "Log Directory: $LOG_DIR"
echo "Binary: $BINARY"
echo "========================================"
echo ""

# Cleanup function
cleanup() {
    echo ""
    echo "Received shutdown signal, stopping all instances..."

    # Stop health servers
    for pid in "${HEALTH_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
    done

    # Stop AudioEnc instances
    for pid in "${PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
    done

    # Wait for processes to stop
    sleep 2

    # Force kill if needed
    for pid in "${PIDS[@]}" "${HEALTH_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -9 "$pid" 2>/dev/null || true
        fi
    done

    echo "All instances stopped"
    exit 0
}

trap cleanup SIGTERM SIGINT

# Start each instance
for i in $(seq 1 "$INSTANCE_COUNT"); do
    INSTANCE_ID=$i
    FIFO_PATH="$FIFO_BASE_PATH/audio${INSTANCE_ID}.fifo"
    PORT=$((PORT_BASE + INSTANCE_ID - 1))
    API_PORT=$((API_PORT_BASE + INSTANCE_ID - 1))
    LOG_FILE="$LOG_DIR/audioenc_instance_${INSTANCE_ID}.log"

    echo "Starting AudioEnc instance $INSTANCE_ID..."
    echo "  TCP Port: $PORT"
    echo "  API Port: $API_PORT"
    echo "  FIFO: $FIFO_PATH"
    echo "  Log: $LOG_FILE"

    # Create or recreate FIFO
    rm -f "$FIFO_PATH"
    mkfifo "$FIFO_PATH" 2>/dev/null || {
        echo "  WARNING: Could not create FIFO (may already exist or need tmpfs with proper permissions)"
    }
    chmod 666 "$FIFO_PATH" 2>/dev/null || true

    # Start audioenc instance in background
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

    echo "  Started AudioEnc with PID: $INSTANCE_PID"

    # Start health endpoint server for this instance
    export INSTANCE_ID="$INSTANCE_ID"
    export API_PORT="$API_PORT"
    export TCP_PORT="$PORT"
    export FIFO_PATH="$FIFO_PATH"
    export ODR_VERSION="v3.1.1"

    # Check if health_server.py exists
    if [ -f "/app/health_server.py" ]; then
        python3 /app/health_server.py > "$LOG_DIR/health_instance_${INSTANCE_ID}.log" 2>&1 &
        HEALTH_PID=$!
        HEALTH_PIDS+=("$HEALTH_PID")
        echo "  Health server started on port $API_PORT (PID: $HEALTH_PID)"
    elif [ -f "/app/src/health_server.py" ]; then
        python3 /app/src/health_server.py > "$LOG_DIR/health_instance_${INSTANCE_ID}.log" 2>&1 &
        HEALTH_PID=$!
        HEALTH_PIDS+=("$HEALTH_PID")
        echo "  Health server started on port $API_PORT (PID: $HEALTH_PID)"
    else
        echo "  WARNING: health_server.py not found, health endpoint not available"
        echo "  Expected locations: /app/health_server.py or /app/src/health_server.py"
    fi

    echo ""

    # Small delay between instances
    sleep 0.5
done

echo "========================================"
echo "All $INSTANCE_COUNT AudioEnc instances started"
echo "AudioEnc PIDs: ${PIDS[*]}"
echo "Health Server PIDs: ${HEALTH_PIDS[*]}"
echo "========================================"
echo ""
echo "Health endpoints available at:"
for i in $(seq 1 "$INSTANCE_COUNT"); do
    API_PORT=$((API_PORT_BASE + i - 1))
    echo "  Instance $i: http://localhost:$API_PORT/health"
done
echo ""
echo "Monitoring instances (checking every 5 seconds)..."

# Monitor instances - restart if any die
RESTART_COUNT=0
MAX_RESTARTS=5

while true; do
    NEED_RESTART=false

    # Check AudioEnc instances
    for i in "${!PIDS[@]}"; do
        pid="${PIDS[$i]}"
        instance_id=$((i + 1))

        if ! kill -0 "$pid" 2>/dev/null; then
            echo "WARNING: AudioEnc instance $instance_id (PID: $pid) died!"
            NEED_RESTART=true
            echo "$(date): Instance $instance_id crashed" >> "$LOG_DIR/crashes.log"
        fi
    done

    # Check health servers (restart if died, but don't trigger container restart)
    for i in "${!HEALTH_PIDS[@]}"; do
        pid="${HEALTH_PIDS[$i]}"
        instance_id=$((i + 1))

        if ! kill -0 "$pid" 2>/dev/null; then
            echo "WARNING: Health server for instance $instance_id (PID: $pid) died, restarting..."

            # Restart health server
            API_PORT=$((API_PORT_BASE + instance_id - 1))
            FIFO_PATH="$FIFO_BASE_PATH/audio${instance_id}.fifo"

            export INSTANCE_ID="$instance_id"
            export API_PORT="$API_PORT"
            export TCP_PORT=$((PORT_BASE + instance_id - 1))
            export FIFO_PATH="$FIFO_PATH"
            export ODR_VERSION="v3.1.1"

            if [ -f "/app/health_server.py" ]; then
                python3 /app/health_server.py > "$LOG_DIR/health_instance_${instance_id}.log" 2>&1 &
                HEALTH_PIDS[$i]=$!
                echo "  Health server restarted (PID: ${HEALTH_PIDS[$i]})"
            fi
        fi
    done

    if [ "$NEED_RESTART" = true ]; then
        RESTART_COUNT=$((RESTART_COUNT + 1))
        echo "AudioEnc instance failure detected (restart count: $RESTART_COUNT/$MAX_RESTARTS)"

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
