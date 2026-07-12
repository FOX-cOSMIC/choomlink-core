// ChoomLink warm-up mod — toolchain validation only (START_HERE §2 "step zero").
// Deploy: copy this file to <game>\r6\scripts\ChoomLinkWarmup\ChoomLinkWarmup.reds
// Verify: launch game, load a save; expect the log line in <game>\red4ext\logs\redscript-*.log
// (FTLog output) and an on-screen warning message shortly after spawn.

@wrapMethod(PlayerPuppet)
protected cb func OnGameAttached() -> Bool {
  let result: Bool = wrappedMethod();
  FTLog("[ChoomLink] warm-up mod active: PlayerPuppet.OnGameAttached fired — redscript toolchain works, choom.");
  GameInstance.GetDelaySystem(this.GetGame())
    .DelayCallback(ChoomLinkWarmupCallback.Create(this), 5.0);
  return result;
}

public class ChoomLinkWarmupCallback extends DelayCallback {
  private let m_player: wref<PlayerPuppet>;

  public static func Create(player: ref<PlayerPuppet>) -> ref<ChoomLinkWarmupCallback> {
    let self = new ChoomLinkWarmupCallback();
    self.m_player = player;
    return self;
  }

  public func Call() -> Void {
    let player = this.m_player;
    if !IsDefined(player) {
      return;
    }
    let msg: SimpleScreenMessage;
    msg.isShown = true;
    msg.duration = 8.0;
    msg.message = "ChoomLink warm-up OK — welcome to Night City, choom.";
    GameInstance.GetBlackboardSystem(player.GetGame())
      .Get(GetAllBlackboardDefs().UI_Notifications)
      .SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(msg), true);
  }
}
