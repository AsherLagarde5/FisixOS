package main

/*
#include <stdint.h>
*/
import "C"

import (
	"encoding/json"
	"fmt"
	"unsafe"
)

var moduleName = C.CString("hello-go")
var moduleDescription = C.CString("Example Go module that echoes its arguments.")

//export PathifyModuleAbiVersion
func PathifyModuleAbiVersion() C.uint32_t {
	return 1
}

//export PathifyModuleName
func PathifyModuleName() *C.char {
	return moduleName
}

//export PathifyModuleDescription
func PathifyModuleDescription() *C.char {
	return moduleDescription
}

//export PathifyModuleRun
func PathifyModuleRun(input *C.uint8_t, inputLength C.uint32_t, output *C.uint8_t, outputCapacity C.uint32_t) C.int32_t {
	if input == nil || output == nil {
		writeOutput(output, outputCapacity, []byte("Input and output buffers are required."), true)
		return -1
	}
	var arguments []string
	if err := json.Unmarshal(unsafe.Slice((*byte)(unsafe.Pointer(input)), int(inputLength)), &arguments); err != nil {
		writeOutput(output, outputCapacity, []byte("Invalid JSON argument array."), true)
		return -1
	}

	message, err := json.Marshal(arguments)
	if err != nil {
		writeOutput(output, outputCapacity, []byte("Could not encode module arguments."), true)
		return -1
	}
	result := []byte(fmt.Sprintf("hello-go received arguments: %s\n", message))
	return C.int32_t(writeOutput(output, outputCapacity, result, false))
}

func writeOutput(output *C.uint8_t, capacity C.uint32_t, value []byte, errorOutput bool) int {
	if output == nil {
		return -1
	}
	buffer := unsafe.Slice((*byte)(unsafe.Pointer(output)), int(capacity))
	if errorOutput {
		if len(buffer) == 0 {
			return -1
		}
		count := copy(buffer[:len(buffer)-1], value)
		buffer[count] = 0
		return -1
	}
	if len(value) > len(buffer) {
		return -1
	}
	return copy(buffer, value)
}

func main() {}
