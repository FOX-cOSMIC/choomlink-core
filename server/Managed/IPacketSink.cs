using Cyberverse.Server.NativeLayer.Protocol.Clientbound;

namespace Cyberverse.Server;

/// <summary>
/// The one seam between game logic and the native transport. GameServer implements it with
/// its existing EnqueueMessage; tests implement it with a recording fake.
/// </summary>
public interface IPacketSink
{
    void EnqueueMessage<T>(EMessageTypeClientbound messageType, uint connectionId, byte channelId, T content)
        where T : struct;
}
