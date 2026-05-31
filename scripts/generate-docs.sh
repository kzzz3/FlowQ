#!/bin/bash
# Generate API documentation using Doxygen

echo "Generating FlowQ API documentation..."

# Check if doxygen is installed
if ! command -v doxygen &> /dev/null; then
    echo "Error: doxygen is not installed."
    echo "Install it with:"
    echo "  - Windows: choco install doxygen.install"
    echo "  - macOS: brew install doxygen"
    echo "  - Linux: sudo apt-get install doxygen"
    exit 1
fi

# Generate documentation
doxygen Doxyfile

echo "Documentation generated in docs/api/html/"
echo "Open docs/api/html/index.html in a browser to view."
