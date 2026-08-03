package pet

import (
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"regexp"
	"sort"
)

type asarNode struct {
	Files     map[string]*asarNode `json:"files"`
	Size      int64                `json:"size"`
	Offset    string               `json:"offset"`
	Unpacked  bool                 `json:"unpacked"`
	Integrity *struct {
		Algorithm string `json:"algorithm"`
		Hash      string `json:"hash"`
	} `json:"integrity"`
}

type asarEntry struct {
	path string
	node *asarNode
}

func readMatchingASARFile(path string, pattern *regexp.Regexp) (string, []byte, error) {
	file, err := os.Open(path)
	if err != nil {
		return "", nil, err
	}
	defer file.Close()

	var prefix [16]byte
	if _, err := io.ReadFull(file, prefix[:]); err != nil {
		return "", nil, fmt.Errorf("read ASAR header: %w", err)
	}
	if binary.LittleEndian.Uint32(prefix[0:4]) != 4 {
		return "", nil, errors.New("unsupported ASAR header")
	}
	headerPickleSize := binary.LittleEndian.Uint32(prefix[4:8])
	jsonSize := binary.LittleEndian.Uint32(prefix[12:16])
	if jsonSize == 0 || jsonSize > 64*1024*1024 {
		return "", nil, errors.New("invalid ASAR JSON header size")
	}
	headerJSON := make([]byte, jsonSize)
	if _, err := io.ReadFull(file, headerJSON); err != nil {
		return "", nil, fmt.Errorf("read ASAR JSON header: %w", err)
	}
	var root asarNode
	if err := json.Unmarshal(headerJSON, &root); err != nil {
		return "", nil, fmt.Errorf("parse ASAR JSON header: %w", err)
	}
	var matches []asarEntry
	walkASAR(&root, "", func(name string, node *asarNode) {
		if pattern.MatchString(name) {
			matches = append(matches, asarEntry{path: name, node: node})
		}
	})
	if len(matches) == 0 {
		return "", nil, errors.New("spritesheet was not found")
	}
	sort.Slice(matches, func(i, j int) bool { return matches[i].path < matches[j].path })
	entry := matches[len(matches)-1]
	if entry.node.Unpacked {
		return "", nil, errors.New("unpacked ASAR pet assets are not supported")
	}
	var relativeOffset int64
	if _, err := fmt.Sscan(entry.node.Offset, &relativeOffset); err != nil || relativeOffset < 0 || entry.node.Size <= 0 {
		return "", nil, errors.New("invalid ASAR file entry")
	}
	data := make([]byte, entry.node.Size)
	dataOffset := int64(8) + int64(headerPickleSize) + relativeOffset
	if _, err := file.ReadAt(data, dataOffset); err != nil {
		return "", nil, fmt.Errorf("read %s: %w", entry.path, err)
	}
	if integrity := entry.node.Integrity; integrity != nil && integrity.Algorithm == "SHA256" && integrity.Hash != "" {
		digest := sha256.Sum256(data)
		if hex.EncodeToString(digest[:]) != integrity.Hash {
			return "", nil, fmt.Errorf("integrity check failed for %s", entry.path)
		}
	}
	return entry.path, data, nil
}

func walkASAR(node *asarNode, prefix string, visit func(string, *asarNode)) {
	for name, child := range node.Files {
		path := name
		if prefix != "" {
			path = prefix + "/" + name
		}
		if len(child.Files) > 0 {
			walkASAR(child, path, visit)
			continue
		}
		visit(path, child)
	}
}
