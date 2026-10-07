param(
    [string]$HostAddress = "127.0.0.1",
    [int]$Port = 17777,
    [int]$Pings = 1000,
    [ValidateSet("", "magic", "version", "oversized", "truncated")]
    [string]$Malformed = ""
)

$ErrorActionPreference = "Stop"
$magic_value = 0x4B4C4C41
$version_value = 1
$header_size = 20

function New-PacketHeader([int]$Magic, [int]$Version, [int]$Type, [int]$PayloadSize, [long]$Sequence) {
    $stream = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($stream)
    $writer.Write([uint32]$Magic)
    $writer.Write([uint16]$Version)
    $writer.Write([uint16]$Type)
    $writer.Write([uint32]$PayloadSize)
    $writer.Write([uint64]$Sequence)
    $writer.Flush()
    return ,$stream.ToArray()
}

function Send-Packet($NetworkStream, [int]$Type, [long]$Sequence, [byte[]]$Payload) {
    $header = New-PacketHeader $magic_value $version_value $Type $Payload.Length $Sequence
    $NetworkStream.Write($header, 0, $header.Length)
    if ($Payload.Length -gt 0) { $NetworkStream.Write($Payload, 0, $Payload.Length) }
}

function Read-Exact($NetworkStream, [int]$Size) {
    $buffer = [byte[]]::new($Size)
    $offset = 0
    while ($offset -lt $Size) {
        $count = $NetworkStream.Read($buffer, $offset, $Size - $offset)
        if ($count -le 0) { throw "Peer closed before packet was complete" }
        $offset += $count
    }
    return ,$buffer
}

function Read-Packet($NetworkStream) {
    $header = Read-Exact $NetworkStream $header_size
    $magic = [BitConverter]::ToUInt32($header, 0)
    $version = [BitConverter]::ToUInt16($header, 4)
    $type = [BitConverter]::ToUInt16($header, 6)
    $size = [BitConverter]::ToUInt32($header, 8)
    $sequence = [BitConverter]::ToUInt64($header, 12)
    if ($magic -ne $magic_value -or $version -ne $version_value -or $size -gt 256MB) {
        throw "Invalid response header"
    }
    return @{ Type = $type; Sequence = $sequence; Payload = (Read-Exact $NetworkStream $size) }
}

$client = [System.Net.Sockets.TcpClient]::new()
$client.ReceiveTimeout = 5000
$client.SendTimeout = 5000
$client.Connect($HostAddress, $Port)
$network = $client.GetStream()
try {
    if ($Malformed) {
        switch ($Malformed) {
            "magic" { $bad = New-PacketHeader 0 $version_value 1 0 1; $network.Write($bad, 0, $bad.Length) }
            "version" {
                $bad = New-PacketHeader $magic_value ($version_value + 1) 1 0 1
                $network.Write($bad, 0, $bad.Length)
                $reply = Read-Packet $network
                $accepted = [System.Text.Encoding]::UTF8.GetString($reply.Payload).Contains('"accepted":true')
                if ($reply.Type -ne 2 -or $reply.Sequence -ne 1 -or $accepted) { throw "Version mismatch was not rejected" }
                Write-Output "Malformed version rejected"
                return
            }
            "oversized" {
                $bad = New-PacketHeader $magic_value $version_value 1 (256MB + 1) 1
                $network.Write($bad, 0, $bad.Length)
            }
            "truncated" {
                $body = [byte[]](0x78)
                $bad = New-PacketHeader $magic_value $version_value 1 4 1
                $network.Write($bad, 0, $bad.Length)
                $network.Write($body, 0, $body.Length)
                $client.Client.Shutdown([System.Net.Sockets.SocketShutdown]::Send)
            }
        }
        $one = [byte[]]::new(1)
        try {
            if ($network.Read($one, 0, 1) -ne 0) { throw "Malformed client remained connected" }
        } catch [System.IO.IOException] { }
        Write-Output "Malformed $Malformed disconnected"
        return
    }

    $hello = [System.Text.Encoding]::UTF8.GetBytes('{"client":"powershell-smoke"}')
    Send-Packet $network 1 1 $hello
    $reply = Read-Packet $network
    $accepted = [System.Text.Encoding]::UTF8.GetString($reply.Payload).Contains('"accepted":true')
    if (($reply.Type -ne 2) -or ($reply.Sequence -ne 1) -or (-not $accepted)) {
        throw "HELLO was rejected"
    }
    for ($sequence = 2; $sequence -lt $Pings + 2; $sequence++) {
        $payload = [BitConverter]::GetBytes([uint64]$sequence)
        Send-Packet $network 3 $sequence $payload
        $reply = Read-Packet $network
        if (($reply.Type -ne 4) -or ($reply.Sequence -ne $sequence) -or
            ([BitConverter]::ToUInt64($reply.Payload, 0) -ne $sequence)) {
            throw "Invalid PONG at sequence $sequence"
        }
    }
    Write-Output "HELLO accepted; $Pings PING/PONG exchanges passed"
}
finally {
    $network.Dispose()
    $client.Dispose()
}
