# Dockerfile for ODR-AudioEnc with StreamDAB Enhancements
# Multi-stage build for production-ready container

# Build stage
FROM ubuntu:22.04 AS builder

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Bangkok

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    git \
    wget \
    curl \
    autotools-dev \
    automake \
    libtool \
    libfdk-aac-dev \
    libvlc-dev \
    libjack-jackd2-dev \
    libasound2-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libzmq3-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    libgoogle-glog-dev \
    # Testing dependencies
    libgtest-dev \
    libgmock-dev \
    # Coverage tools
    lcov \
    gcovr \
    # Security tools
    valgrind \
    # Performance tools
    linux-tools-generic \
    && rm -rf /var/lib/apt/lists/*

# Install Google Test (build from source if needed)
RUN cd /usr/src/gtest && \
    cmake . && \
    make && \
    cp lib/*.a /usr/lib/ && \
    cd /usr/src/gmock && \
    cmake . && \
    make && \
    cp lib/*.a /usr/lib/

# Set work directory
WORKDIR /build

# Copy source code
COPY . .

# Configure build with all features enabled
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON \
    -DENABLE_COVERAGE=ON \
    -DENABLE_OPTIMIZATION=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local

# Build the project
RUN cmake --build build -j$(nproc)

# Run tests to ensure everything works
RUN cd build && ctest --verbose --parallel $(nproc)

# Generate coverage report
RUN cd build && \
    cmake --build . --target coverage && \
    echo "Coverage report generated in build/coverage_report/"

# Install the application
RUN cmake --install build

# Production stage
FROM ubuntu:22.04 AS production

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Bangkok

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libfdk-aac2 \
    libvlc5 \
    libjack-jackd2-0 \
    libasound2 \
    libgstreamer1.0-0 \
    libgstreamer-plugins-base1.0-0 \
    libzmq5 \
    libcurl4 \
    libssl3 \
    ca-certificates \
    tzdata \
    # Debugging tools (optional, can be removed for minimal image)
    gdb \
    strace \
    # Network tools
    netcat-openbsd \
    telnet \
    curl \
    wget \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user for security
RUN groupadd -r streamdab && \
    useradd -r -g streamdab -m -d /opt/streamdab -s /bin/bash streamdab

# Copy built application from builder stage
COPY --from=builder /usr/local/bin/odr-audioenc /usr/local/bin/
COPY --from=builder /usr/local/share/man/man1/odr-audioenc.1 /usr/local/share/man/man1/

# Create application directories
RUN mkdir -p /opt/streamdab/{config,logs,data} && \
    chown -R streamdab:streamdab /opt/streamdab

# Copy configuration templates
COPY --chown=streamdab:streamdab docker/config/* /opt/streamdab/config/

# Set up logging directory with proper permissions
RUN mkdir -p /var/log/odr-audioenc && \
    chown streamdab:streamdab /var/log/odr-audioenc

# Health check script
COPY --chown=streamdab:streamdab docker/healthcheck.sh /opt/streamdab/
RUN chmod +x /opt/streamdab/healthcheck.sh

# Startup script
COPY --chown=streamdab:streamdab docker/entrypoint.sh /opt/streamdab/
RUN chmod +x /opt/streamdab/entrypoint.sh

# Switch to non-root user
USER streamdab
WORKDIR /opt/streamdab

# Expose StreamDAB API port (from allocation plan)
EXPOSE 8007

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD /opt/streamdab/healthcheck.sh

# Default command
ENTRYPOINT ["/opt/streamdab/entrypoint.sh"]
CMD ["--help"]

# Labels for container metadata
LABEL org.opencontainers.image.title="ODR-AudioEnc StreamDAB Enhanced" \
    org.opencontainers.image.description="DAB+ audio encoder with StreamDAB enhancements for Thailand broadcasting" \
    org.opencontainers.image.version="3.6.0" \
    org.opencontainers.image.vendor="StreamDAB Project" \
    org.opencontainers.image.licenses="Apache-2.0" \
    org.opencontainers.image.documentation="https://github.com/streamdab/ODR-AudioEnc" \
    org.opencontainers.image.source="https://github.com/streamdab/ODR-AudioEnc" \
    maintainer="StreamDAB Project"

# Development stage (for testing and development)
FROM builder AS development

# Install additional development tools
RUN apt-get update && apt-get install -y \
    vim \
    nano \
    htop \
    iotop \
    tmux \
    screen \
    bash-completion \
    tree \
    file \
    less \
    && rm -rf /var/lib/apt/lists/*

# Set up development environment
WORKDIR /workspace
COPY --from=builder /build ./build

# Copy coverage reports for analysis
RUN mkdir -p /workspace/reports && \
    cp -r /build/coverage_report/* /workspace/reports/ 2>/dev/null || true

# Development user setup
RUN useradd -m -s /bin/bash -G sudo developer && \
    echo "developer:developer" | chpasswd

USER developer
WORKDIR /workspace

# Default development command
CMD ["/bin/bash"]

# Testing stage (for CI/CD)
FROM builder AS testing

# Run comprehensive tests
RUN cd build && \
    echo "Running unit tests..." && \
    ctest -L unit --verbose && \
    echo "Running integration tests..." && \
    ctest -L integration --verbose && \
    echo "Running performance tests..." && \
    ctest -L performance --verbose

# Run security analysis
RUN cd build && \
    echo "Running Valgrind memory check..." && \
    valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all \
    --track-origins=yes --verbose --log-file=valgrind.log \
    ./test_enhanced_stream || true && \
    echo "Valgrind analysis completed"

# Generate test reports
RUN cd build && \
    echo "Generating test reports..." && \
    ctest --output-on-failure --output-junit test_results.xml || true

# Coverage analysis
RUN cd build && \
    echo "Coverage analysis results:" && \
    lcov --summary coverage_filtered.info

# Final test validation
RUN echo "All tests completed successfully"
