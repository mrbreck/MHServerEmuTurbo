using System.Net;
using MHServerEmu.Core.Logging;
using MHServerEmu.Core.Network.Web;
using MHServerEmu.Games.GameData;
using MHServerEmu.Games.GameData.Prototypes;
using MHServerEmu.WebFrontend.Models;
using MHServerEmu.WebFrontend.Network;

namespace MHServerEmu.WebFrontend.Handlers.GameOptions
{
    /// <summary>
    /// GET  /api/gameoptions?email=x&amp;token=y
    ///   Returns the available rarities and the current per-slot vaporize thresholds.
    ///   Response: <see cref="GameOptionsResponse"/>
    ///
    /// POST /api/gameoptions
    ///   Body: <see cref="GameOptionsRequest"/>
    ///   Sets thresholds. A <see cref="ServiceMessage.VaporizerSlot.RarityId"/> of 0 disables
    ///   vaporization for that slot.
    ///
    /// Auth is via PlatformTicket (email + token issued at login).
    /// </summary>
    public class GameOptionsWebHandler : WebHandler
    {
        private static readonly Logger Logger = LogManager.CreateLogger();

        // Rarity list is static game data — build once and cache forever.
        private List<RarityDto> _cachedRarities = null;

        protected override async Task Get(WebRequestContext context)
        {
            string email = context.UrlQuery["email"];
            string token = context.UrlQuery["token"];

            if (string.IsNullOrWhiteSpace(email) || string.IsNullOrWhiteSpace(token))
            {
                context.StatusCode = (int)HttpStatusCode.BadRequest;
                await context.SendJsonAsync(new { Error = "email and token query parameters are required" });
                return;
            }

            var serviceResponse = await GameServiceTaskManager.Instance.GetGameOptionsAsync(email, token);

            if (serviceResponse.StatusCode != (int)HttpStatusCode.OK)
            {
                context.StatusCode = serviceResponse.StatusCode;
                await context.SendJsonAsync(new { Error = "Authentication failed or player is not currently in game" });
                return;
            }

            await context.SendJsonAsync(new GameOptionsResponse(GetCachedRarities(), serviceResponse.VaporizerSlots ?? new()));
        }

        protected override async Task Post(WebRequestContext context)
        {
            var request = await context.ReadJsonAsync<GameOptionsRequest>();

            if (string.IsNullOrWhiteSpace(request.Email) || string.IsNullOrWhiteSpace(request.Token))
            {
                context.StatusCode = (int)HttpStatusCode.BadRequest;
                await context.SendJsonAsync(new { Error = "Email and Token are required" });
                return;
            }

            var serviceResponse = await GameServiceTaskManager.Instance.SetGameOptionsAsync(
                request.Email, request.Token, request.VaporizerSlots ?? new());

            context.StatusCode = serviceResponse.StatusCode;
            if (serviceResponse.StatusCode != (int)HttpStatusCode.OK)
                await context.SendJsonAsync(new { Error = "Authentication failed, player not in game, or invalid prototype ID" });
        }

        private List<RarityDto> GetCachedRarities()
        {
            if (_cachedRarities != null) return _cachedRarities;

            var rarities = new List<RarityDto>();

            foreach (PrototypeId protoRef in GameDatabase.DataDirectory
                .IteratePrototypesInHierarchy<RarityPrototype>(PrototypeIterateFlags.NoAbstractApprovedOnly))
            {
                RarityPrototype proto = protoRef.As<RarityPrototype>();
                if (proto == null) continue;

                string name = GameDatabase.GetFormattedPrototypeName(protoRef);
                int slash = name.LastIndexOf('/');
                if (slash >= 0) name = name[(slash + 1)..];

                string display = name switch {
                    "R1Normal"    => "Common",
                    "R2Uncommon"  => "Uncommon",
                    "R3Rare"      => "Rare",
                    "R4Epic"      => "Epic",
                    "R5Legendary" => "Cosmic",
                    _ => null
                };
                if (display == null) continue;

                rarities.Add(new RarityDto((ulong)protoRef, display, proto.Tier));
            }

            rarities.Sort((a, b) => a.Tier.CompareTo(b.Tier));
            _cachedRarities = rarities;
            return _cachedRarities;
        }
    }
}
