# Filesystem Extension

The filesystem extension provides file and directory management capabilities through IOTMP protocol, supporting both inline transfers for small files and streaming for large files.

## Architecture

The extension follows REST principles with proper separation of concerns:
- **PARAMETERS field (2)**: Configuration options and metadata
- **PAYLOAD field (3)**: Actual file data or response content

## Endpoints

All endpoints use wildcard paths in the URL for cleaner API design. Remember that in IOTMP, all operations are `RUN_RESOURCE` - the "verb" is part of the resource name.

### `$fs/get/*path`
Unified endpoint for listing directories, getting file info, and downloading files.

**URL Pattern**: `$fs/get/path/to/resource`
- Path is captured via wildcard `*path`

**Parameters** (in PARAMETERS field):
- `info` (bool): Return metadata only, don't download/list
- `include_hidden` (bool): Include hidden files in directory listing
- `recursive` (bool): Recursive directory listing

**Examples**:
```
# List directory
RUN $fs/get/home/user/
Parameters: {}

# Get file info
RUN $fs/get/home/user/file.txt
Parameters: {"info": true}

# Download file (inline, for files < 64KB)
RUN $fs/get/home/user/file.txt
Parameters: {}

# List with hidden files
RUN $fs/get/home/
Parameters: {"include_hidden": true}
```

### `$fs/put/*path`
Create files, directories, or update existing files.

**URL Pattern**: `$fs/put/path/to/resource`
- Path is captured via wildcard `*path`
- Ending with `/` creates a directory
- Without `/` creates/updates a file

**Parameters** (in PARAMETERS field):
- `overwrite` (bool, default: true): Allow overwriting existing files

**Payload** (in PAYLOAD field):
- `data` (bytes): File content for file operations

**Examples**:
```
# Create directory
RUN $fs/put/home/user/newfolder/
Parameters: {}
Payload: {}

# Create/update file with content
RUN $fs/put/home/user/file.txt
Parameters: {}
Payload: {"data": <bytes>}

# Create empty file
RUN $fs/put/home/user/empty.txt
Parameters: {}
Payload: {}

# Safe create (fail if exists)
RUN $fs/put/home/user/file.txt
Parameters: {"overwrite": false}
Payload: {"data": <bytes>}
```

### `$fs/delete/*path`
Remove files or directories.

**URL Pattern**: `$fs/delete/path/to/resource`
- Path is captured via wildcard `*path`

**Parameters** (in PARAMETERS field):
- None required

**Examples**:
```
# Delete file
RUN $fs/delete/home/user/file.txt
Parameters: {}

# Delete directory
RUN $fs/delete/home/user/folder/
Parameters: {}
```

### `$fs/move/*source`
Move or rename files and directories.

**URL Pattern**: `$fs/move/source/path`
- Source path is captured via wildcard `*source`

**Parameters** (in PARAMETERS field):
- `destination` (string): Destination path
- `overwrite` (bool, default: false): Allow overwriting destination

**Examples**:
```
# Rename file
RUN $fs/move/home/user/old.txt
Parameters: {
  "destination": "/home/user/new.txt"
}

# Move to different directory
RUN $fs/move/home/user/file.txt
Parameters: {
  "destination": "/home/backup/file.txt",
  "overwrite": true
}
```

## Streaming Operations

For large file transfers (> 64KB), streaming sessions are used:

### Download Stream
```
START_STREAM $fs/download/:session
Parameters: {
  "path": "/path/to/large/file.bin",
  "chunk_size": 65536,        # Optional, default: 64KB
  "max_bandwidth_mbps": 10    # Optional, 0 = unlimited
}
```

### Upload Stream
```
START_STREAM $fs/upload/:session
Parameters: {
  "path": "/path/to/new/file.bin",
  "overwrite": true           # Optional, default: true
}
```

## File Size Limits

- **Inline transfers**: Maximum 64KB
  - Files ≤ 64KB are transferred directly in the response
  - Larger files return error 413 with size information
  
- **Streaming transfers**: No size limit
  - Automatic flow control with acknowledgments
  - Configurable chunk size and bandwidth limiting

## Flow Control (Streaming)

- **MAX_CHUNKS_IN_FLIGHT**: 4 unacknowledged chunks maximum
- **ACK_TIMEOUT**: 10 seconds timeout waiting for acknowledgments
- **Bandwidth limiting**: Optional Mbps limit for downloads

## Security

- **Path validation**: All paths are validated to prevent directory traversal
- **Base path restriction**: Optional base path to restrict access
- **Overwrite protection**: Explicit `overwrite` parameter required for safety

## Response Format

### Success Responses

Directory listing:
```json
{
  "path": "/home/user/",
  "count": 3,
  "entries": [
    {
      "name": "file.txt",
      "type": "file",
      "size": 1024,
      "mode": "rw-r--r--",
      "modified": 1234567890
    },
    {
      "name": "folder",
      "type": "directory",
      "size": 0,
      "mode": "rwxr-xr-x",
      "modified": 1234567890
    }
  ]
}
```

File info:
```json
{
  "name": "file.txt",
  "path": "/home/user/file.txt",
  "type": "file",
  "size": 1024,
  "mode": "rw-r--r--",
  "modified": 1234567890
}
```

File download (inline):
- Binary data returned directly as bytes

Operation success:
```json
{
  "success": true,
  "path": "/home/user/file.txt",
  "type": "file",
  "size": 1024
}
```

### Error Responses

```json
{
  "error": "Error message",
  "code": 404  // Optional HTTP-like status code
}
```

Common error codes:
- 400: Bad request (invalid parameters)
- 403: Access denied (path outside allowed directory)
- 404: Path does not exist
- 409: Conflict (file exists when overwrite=false)
- 413: File too large for inline transfer

## Implementation Details

The filesystem extension is implemented using:
- C++17 `<filesystem>` library for cross-platform file operations
- Stream sessions for large file transfers with flow control
- Bandwidth limiting using Boost.Asio timers
- Proper separation of inline vs streaming transfers based on file size