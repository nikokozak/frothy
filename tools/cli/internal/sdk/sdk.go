package sdk

import (
	"embed"
	"io/fs"
)

const (
	payloadDir            = "generated"
	payloadArchiveName    = "kernel.tar.gz"
	payloadVersionName    = "VERSION"
	payloadDigestFilename = "PAYLOAD_SHA256"
)

//go:embed generated/VERSION generated/PAYLOAD_SHA256 generated/kernel.tar.gz
var PayloadFS embed.FS

func EmbeddedPayloadRoot() (fs.FS, error) {
	return embeddedPayloadFS()
}
