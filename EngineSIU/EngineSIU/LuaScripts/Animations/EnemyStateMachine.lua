AnimFSM = {
    currentState = "Idle",
    -- idleAnimation = "Contents/Enemy_Idle/Armature|Enemy_Idle",
    idleAnimation = "Contents/Injured_Idle/Armature|Injured_Idle",
    attackAnimations = {
        "Contents/Horizontal1/Armature|Horizontal1",
        "Contents/Horizontal2/Armature|Horizontal2",
        "Contents/Vertical1/Armature|Vertical1",
    },
    reactionAnimation = "Contents/Enemy_Impact/Armature|Enemy_Impact",
    kneelAnimation = "Contents/Injured_Idle/Armature|Injured_Idle",
    kneelIdleAnimation = "Contents/Kneel_Idle/Armature|Kneel_Idle",
    attackCooldown = 2.5,
    isAttacking = false,
    isReacting = false,
    isDefeated = false,
    lastAttackTime = 0,
    reactionStartTime = 0,
    attackStartTime = 0,
    defeatStartTime = 0,
    timer = 0,

    TransitionToState = function(self, newState)
        self.currentState = newState
    end,

    Update = function(self, dt)
        self.timer = self.timer + dt
        -- print(self.timer)
        -- print(self.currentState)

        if self.GameState <= 1 or self.GameState == 4 then
            return {
            anim = self.idleAnimation,
            blend = 0.0,
            loop = true,
            rate_scale = 1.0,
            state = self.currentState
            }
        end

        if self.currentState == "Reacting" then
            return self:HandleReactionState()
        elseif self.currentState == "Attacking" then
            return self:HandleAttackState()
        elseif self.currentState == "Defeat" then
            return self:HandleDefeatState()
        else
            return self:HandleIdleState(dt)
        end
    end,

    HandleIdleState = function(self, dt)
        -- 공격 쿨타임 체크
        if self.timer - self.lastAttackTime > self.attackCooldown then
            self:TransitionToState("Attacking")
        end

        return {
            anim = self.idleAnimation,
            blend = 0.2,
            loop = true,
            rate_scale = 1.0,
            state = self.currentState
        }
    end,

    HandleAttackState = function(self)
        if not self.isAttacking then
            self.isAttacking = true
            self.selectedAttack = self.attackAnimations[math.random(#self.attackAnimations)]
            self.attackStartTime = self.timer;
        end
        -- print("ATTACK DURATION")
        -- print(self.CurrentAnimDuration)
        if self.timer > self.attackStartTime + self.CurrentAnimDuration / 0.8 then
            self.isAttacking = false
            self.lastAttackTime = self.timer
            self:TransitionToState("Idle")
        end

        return {
            anim = self.selectedAttack,
            blend = 0.2,
            loop = false,
            rate_scale = 0.8,
            state = self.currentState
        }
    end,

    HandleReactionState = function(self)
        if not self.isReacting then
            self.isReacting = true
            self.isAttacking = false
            self.reactionStartTime = self.timer;
        end
        -- print("REACTION DURATION")
        -- print(self.CurrentAnimDuration)
        if self.reactionStartTime and self.timer > self.reactionStartTime + self.CurrentAnimDuration then
            self.isReacting = false
            self.lastAttackTime = self.timer
            self:TransitionToState("Idle")
        end

        return {
            anim = self.reactionAnimation,
            blend = 0.5,
            loop = false,
            rate_scale = 1.0,
            state = self.currentState
        }
    end,

    HandleDefeatState = function(self)
        if not self.isDefeated then
            -- 적 패배 진입 시 Kneel 애니메이션 실행
            self.isDefeated = true
            self.defeatStartTime = self.timer
            return {
                anim = self.kneelAnimation,
                blend = 0.3,
                loop = true,
                rate_scale = 1.0,
                state = self.currentState
            }
        else
            local kneelDuration = self.CurrentAnimDuration
            -- local kneelDuration = 0.4 
            -- if self.timer > self.defeatStartTime + kneelDuration then
            --     return {
            --         anim = self.kneelIdleAnimation,
            --         blend = 4.0,
            --         loop = true,
            --         rate_scale = 1.0,
            --         state = self.currentState
            --     }
            if self.timer > self.defeatStartTime + kneelDuration then
                return {
                    anim = self.kneelAnimation,
                    blend = 0.0,
                    loop = true,
                    rate_scale = 1.0,
                    state = self.currentState
                }
            -- else
            --     return {
            --     anim = self.kneelAnimation,
            --     blend = 0.0,
            --     loop = false,
            --     rate_scale = 1.0,
            --     state = self.currentState
            -- }
            end
        end
    end,
}

return AnimFSM
