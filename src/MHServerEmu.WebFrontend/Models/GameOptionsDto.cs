using MHServerEmu.Core.Network;

namespace MHServerEmu.WebFrontend.Models
{
    /// <summary>
    /// Rarity entry returned in the GET /api/gameoptions response.
    /// </summary>
    public readonly struct RarityDto
    {
        public ulong  ProtoId { get; init; }
        public string Name    { get; init; }
        public int    Tier    { get; init; }

        public RarityDto(ulong protoId, string name, int tier)
        {
            ProtoId = protoId;
            Name    = name;
            Tier    = tier;
        }
    }

    /// <summary>
    /// GET /api/gameoptions response body.
    /// </summary>
    public sealed class GameOptionsResponse
    {
        public List<RarityDto>                    Rarities       { get; init; }
        public List<ServiceMessage.VaporizerSlot> VaporizerSlots { get; init; }

        public GameOptionsResponse(List<RarityDto> rarities, List<ServiceMessage.VaporizerSlot> vaporizerSlots)
        {
            Rarities       = rarities;
            VaporizerSlots = vaporizerSlots;
        }
    }

    /// <summary>
    /// POST /api/gameoptions request body.
    /// A <see cref="ServiceMessage.VaporizerSlot.RarityId"/> of 0 disables vaporization for that slot.
    /// </summary>
    public sealed class GameOptionsRequest
    {
        public string                             Email          { get; init; }
        public string                             Token          { get; init; }
        public List<ServiceMessage.VaporizerSlot> VaporizerSlots { get; init; }
    }
}
