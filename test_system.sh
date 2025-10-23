#!/bin/bash

# Network File System - Testing Script
# Tests basic functionality of the system

echo "================================"
echo "NFS System Testing Script"
echo "================================"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if executables exist
check_build() {
    echo "Checking if system is built..."
    if [ ! -f "nm" ] || [ ! -f "ss" ] || [ ! -f "client" ]; then
        echo -e "${RED}Error: Executables not found. Run 'make all' first.${NC}"
        exit 1
    fi
    echo -e "${GREEN}✓ All executables found${NC}"
    echo ""
}

# Clean previous test artifacts
clean_test() {
    echo "Cleaning previous test artifacts..."
    rm -f *.log
    rm -rf ss_storage_*
    rm -rf test_storage_*
    echo -e "${GREEN}✓ Cleaned${NC}"
    echo ""
}

# Create test files
create_test_files() {
    echo "Creating test storage directories..."
    
    # Create storage for SS1
    mkdir -p test_storage_1
    echo "Hello world. This is a test file." > test_storage_1/test1.txt
    echo "Another test file with multiple sentences. How are you? Fine!" > test_storage_1/test2.txt
    echo "ls -la" > test_storage_1/script.txt
    
    # Create storage for SS2
    mkdir -p test_storage_2
    echo "Sample document on second server." > test_storage_2/sample.txt
    
    echo -e "${GREEN}✓ Test files created${NC}"
    echo "  - test_storage_1/test1.txt"
    echo "  - test_storage_1/test2.txt"
    echo "  - test_storage_1/script.txt"
    echo "  - test_storage_2/sample.txt"
    echo ""
}

# Instructions for manual testing
manual_test_instructions() {
    echo "================================"
    echo "MANUAL TESTING INSTRUCTIONS"
    echo "================================"
    echo ""
    
    echo -e "${YELLOW}1. Start Name Server (Terminal 1):${NC}"
    echo "   ./nm"
    echo ""
    
    echo -e "${YELLOW}2. Start Storage Server 1 (Terminal 2):${NC}"
    echo "   # First, copy test files to its directory"
    echo "   cp -r test_storage_1/* ss_storage_<pid>/ (after SS starts)"
    echo "   ./ss 127.0.0.1 8080 9001"
    echo ""
    
    echo -e "${YELLOW}3. Start Storage Server 2 (Terminal 3):${NC}"
    echo "   cp -r test_storage_2/* ss_storage_<pid>/ (after SS starts)"
    echo "   ./ss 127.0.0.1 8080 9002"
    echo ""
    
    echo -e "${YELLOW}4. Start Client (Terminal 4):${NC}"
    echo "   ./client"
    echo "   Enter username: alice"
    echo ""
    
    echo "================================"
    echo "TEST CASES TO TRY:"
    echo "================================"
    echo ""
    
    echo -e "${GREEN}Test 1: View Files${NC}"
    echo "  > VIEW"
    echo "  > VIEW -a"
    echo "  > VIEW -l"
    echo ""
    
    echo -e "${GREEN}Test 2: Create File${NC}"
    echo "  > CREATE myfile.txt"
    echo "  > VIEW"
    echo ""
    
    echo -e "${GREEN}Test 3: Write to File${NC}"
    echo "  > WRITE myfile.txt 0"
    echo "  Client: 1 Hello"
    echo "  Client: 2 world."
    echo "  Client: ETIRW"
    echo "  > READ myfile.txt"
    echo ""
    
    echo -e "${GREEN}Test 4: Delimiter Handling${NC}"
    echo "  > WRITE myfile.txt 1"
    echo "  Client: 1 How"
    echo "  Client: 2 are"
    echo "  Client: 3 you?"
    echo "  Client: ETIRW"
    echo "  > READ myfile.txt"
    echo "  Expected: 'Hello world. How are you?'"
    echo ""
    
    echo -e "${GREEN}Test 5: Info${NC}"
    echo "  > INFO myfile.txt"
    echo ""
    
    echo -e "${GREEN}Test 6: Access Control (Start 2nd client as 'bob')${NC}"
    echo "  # As alice:"
    echo "  > ADDACCESS -R myfile.txt bob"
    echo "  "
    echo "  # As bob (new client):"
    echo "  > READ myfile.txt  # Should work"
    echo "  > WRITE myfile.txt 0  # Should fail (no write access)"
    echo "  "
    echo "  # As alice again:"
    echo "  > ADDACCESS -W myfile.txt bob"
    echo "  "
    echo "  # As bob:"
    echo "  > WRITE myfile.txt 0  # Should work now"
    echo ""
    
    echo -e "${GREEN}Test 7: Concurrent Write (2 clients)${NC}"
    echo "  # Both clients try to WRITE same sentence"
    echo "  # Second one should get 'Sentence locked' error"
    echo ""
    
    echo -e "${GREEN}Test 8: Stream${NC}"
    echo "  > STREAM myfile.txt"
    echo "  # Should display words with 0.1s delay"
    echo ""
    
    echo -e "${GREEN}Test 9: Undo${NC}"
    echo "  > WRITE myfile.txt 0"
    echo "  Client: 1 CHANGED"
    echo "  Client: ETIRW"
    echo "  > READ myfile.txt"
    echo "  > UNDO myfile.txt"
    echo "  > READ myfile.txt  # Should revert"
    echo ""
    
    echo -e "${GREEN}Test 10: Execute${NC}"
    echo "  # If you have script.txt with 'ls -la'"
    echo "  > EXEC script.txt"
    echo ""
    
    echo -e "${GREEN}Test 11: Delete${NC}"
    echo "  > DELETE myfile.txt"
    echo "  > VIEW  # Should not show myfile.txt"
    echo ""
    
    echo -e "${GREEN}Test 12: List Users${NC}"
    echo "  > LIST"
    echo ""
    
    echo "================================"
    echo "EDGE CASES TO TEST:"
    echo "================================"
    echo ""
    
    echo "1. Invalid indices:"
    echo "   > WRITE myfile.txt 999  # Out of range"
    echo ""
    
    echo "2. File not found:"
    echo "   > READ nonexistent.txt"
    echo ""
    
    echo "3. Not owner:"
    echo "   # As bob, try to delete alice's file"
    echo "   > DELETE myfile.txt  # Should fail"
    echo ""
    
    echo "4. Kill SS during STREAM:"
    echo "   > STREAM myfile.txt"
    echo "   # Kill SS process mid-stream"
    echo "   # Should show error"
    echo ""
}

# Automated compilation check
compile_check() {
    echo "Testing compilation..."
    make clean > /dev/null 2>&1
    
    if make all > /dev/null 2>&1; then
        echo -e "${GREEN}✓ Compilation successful${NC}"
    else
        echo -e "${RED}✗ Compilation failed${NC}"
        echo "Run 'make all' to see errors"
        exit 1
    fi
    echo ""
}

# Main execution
main() {
    clear
    
    echo ""
    echo "This script will:"
    echo "1. Check if system is built"
    echo "2. Clean previous artifacts"
    echo "3. Create test files"
    echo "4. Provide manual testing instructions"
    echo ""
    
    read -p "Continue? (y/n) " -n 1 -r
    echo ""
    
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 0
    fi
    
    echo ""
    
    compile_check
    check_build
    clean_test
    create_test_files
    manual_test_instructions
    
    echo ""
    echo "================================"
    echo "READY TO TEST!"
    echo "================================"
    echo ""
    echo "Open 4 terminals and follow the instructions above."
    echo ""
    echo "Logs will be created:"
    echo "  - nm.log (Name Server)"
    echo "  - ss_<pid>.log (each Storage Server)"
    echo ""
    echo "Check logs for detailed operation traces."
    echo ""
}

# Run main
main
