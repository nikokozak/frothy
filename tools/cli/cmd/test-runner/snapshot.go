package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"hash/crc32"
	"io"
	"os"
)

func commandCorruptSnapshot(args []string) error {
	fs := flag.NewFlagSet("corrupt-snapshot", flag.ContinueOnError)
	path := fs.String("path", "", "snapshot path")
	cellspaceCapacity := fs.Uint("cellspace-capacity", 256, "cellspace capacity to exceed")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if *path == "" {
		return fmt.Errorf("corrupt-snapshot requires --path")
	}
	return corruptSnapshot(*path, uint32(*cellspaceCapacity))
}

func corruptSnapshot(path string, cellspaceCapacity uint32) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	if len(data) < snapshotHeaderSize {
		return fmt.Errorf("snapshot too small: %s", path)
	}

	header := append([]byte(nil), data[:snapshotHeaderSize]...)
	payload := append([]byte(nil), data[snapshotHeaderSize:]...)
	cursor := 0

	readU8 := func() (uint8, error) {
		if cursor+1 > len(payload) {
			return 0, io.ErrUnexpectedEOF
		}
		value := payload[cursor]
		cursor++
		return value, nil
	}
	readU16 := func() (uint16, error) {
		if cursor+2 > len(payload) {
			return 0, io.ErrUnexpectedEOF
		}
		value := binary.LittleEndian.Uint16(payload[cursor:])
		cursor += 2
		return value, nil
	}
	readU32 := func() (uint32, error) {
		if cursor+4 > len(payload) {
			return 0, io.ErrUnexpectedEOF
		}
		value := binary.LittleEndian.Uint32(payload[cursor:])
		cursor += 4
		return value, nil
	}
	skipPersistableCell := func() error {
		kind, err := readU8()
		if err != nil {
			return err
		}
		switch kind {
		case 0, 1, 3, 4, 5, 7:
			_, err = readU32()
			return err
		case 2:
			_, err = readU16()
			return err
		case frothCallTag:
			return fmt.Errorf("unexpected CALL in persisted cell")
		default:
			return fmt.Errorf("unexpected persistable cell kind: %d", kind)
		}
	}

	nameCount, err := readU16()
	if err != nil {
		return err
	}
	for i := uint16(0); i < nameCount; i++ {
		nameLen, err := readU16()
		if err != nil {
			return err
		}
		cursor += int(nameLen)
		if cursor > len(payload) {
			return io.ErrUnexpectedEOF
		}
	}

	objectCount, err := readU32()
	if err != nil {
		return err
	}
	for i := uint32(0); i < objectCount; i++ {
		if _, err := readU8(); err != nil {
			return err
		}
		if _, err := readU32(); err != nil {
			return err
		}
		objectLen, err := readU32()
		if err != nil {
			return err
		}
		cursor += int(objectLen)
		if cursor > len(payload) {
			return io.ErrUnexpectedEOF
		}
	}

	bindingCount, err := readU32()
	if err != nil {
		return err
	}
	for i := uint32(0); i < bindingCount; i++ {
		if _, err := readU16(); err != nil {
			return err
		}
		if err := skipPersistableCell(); err != nil {
			return err
		}
		if _, err := readU32(); err != nil {
			return err
		}
		if _, err := readU16(); err != nil {
			return err
		}
		if _, err := readU16(); err != nil {
			return err
		}
	}

	if cursor+4 > len(payload) {
		return io.ErrUnexpectedEOF
	}
	binary.LittleEndian.PutUint32(payload[cursor:], cellspaceCapacity+1)
	binary.LittleEndian.PutUint32(header[payloadCRC32Offset:], crc32.ChecksumIEEE(payload))
	binary.LittleEndian.PutUint32(header[headerCRC32Offset:], 0)
	binary.LittleEndian.PutUint32(header[headerCRC32Offset:], crc32.ChecksumIEEE(header))
	return os.WriteFile(path, append(header, payload...), 0o644)
}
