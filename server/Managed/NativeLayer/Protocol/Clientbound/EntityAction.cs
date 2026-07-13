using System.Runtime.InteropServices;
using Cyberverse.Server.NativeLayer.Protocol.Common;
using Cyberverse.Server.NativeLayer.Protocol.Serverbound;

namespace Cyberverse.Server.NativeLayer.Protocol.Clientbound;

[StructLayout(LayoutKind.Sequential, Pack = 8)]
// Relays a serverbound PlayerActionTracked (jump, later swim/climb/...) to the other clients.
public struct EntityAction : IClientBoundPacket
{
    public ulong networkedEntityId;
    public EPlayerAction action;
    public Vector3 worldTransform;

    public EMessageTypeClientbound GetMessageType() => EMessageTypeClientbound.EntityAction;
}
