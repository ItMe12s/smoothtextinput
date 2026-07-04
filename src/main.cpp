#include "animation.hpp"

#include <Geode/modify/CCTextInputNode.hpp>
#include <Geode/binding/MultilineBitmapFont.hpp>
#include <Geode/binding/TextArea.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace geode::prelude;
using namespace smoothtextinput;

class $modify(CharFadeInput, CCTextInputNode) {
    struct Fields {
        std::string prevString;
        std::vector<Ghost> prevLabel;
        std::vector<std::optional<PendingFade>> pending;
        bool externalUpdate = false;
    };

    struct TextAreaGlyphs {
        CCArray* chars = nullptr;
        std::vector<size_t> rawToGlyph;
    };

    static constexpr size_t kNoGlyph = std::numeric_limits<size_t>::max();

    CCNode* activeLabel() {
        if (m_textArea && m_textArea->m_label) return m_textArea->m_label;
        return m_textLabel;
    }

    GLubyte activeLabelOpacity() {
        if (m_textArea && m_textArea->m_label) return m_textArea->m_label->getOpacity();
        return m_textLabel ? m_textLabel->getOpacity() : 255;
    }

    TextAreaGlyphs textAreaGlyphs(std::string_view text) const {
        if (!m_textArea || !m_textArea->m_label) return {};
        auto chars = m_textArea->m_label->m_characters;
        if (!chars) return {};

        std::vector<size_t> map(text.size(), kNoGlyph);
        size_t glyph = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '\n' && text[i] != '\r') map[i] = glyph++;
        }
        return {chars, std::move(map)};
    }

    CCSprite* glyphAt(std::string_view text, size_t i, TextAreaGlyphs const* ta) const {
        if (i >= text.size()) return nullptr;

        if (m_textArea && m_textArea->m_label) {
            if (text[i] == '\n' || text[i] == '\r') return nullptr;
            if (!ta || !ta->chars || i >= ta->rawToGlyph.size()) return nullptr;
            auto gi = ta->rawToGlyph[i];
            if (gi == kNoGlyph || gi >= ta->chars->count()) return nullptr;
            return typeinfo_cast<CCSprite*>(ta->chars->objectAtIndex(static_cast<unsigned int>(gi)));
        }

        if (!m_textLabel) return nullptr;
        return typeinfo_cast<CCSprite*>(m_textLabel->getChildByTag(static_cast<int>(i)));
    }

    void skipAnimate() {
        purgeGhosts();
        plainRefresh();
    }

    void purgeGhostsFromParent(CCNode* parent) {
        if (!parent) return;
        auto children = parent->getChildren();
        if (!children) return;

        std::vector<CCNode*> toRemove;
        for (unsigned int j = 0; j < children->count(); ++j) {
            auto node = typeinfo_cast<CCNode*>(children->objectAtIndex(j));
            if (node && node->getTag() == kGhostTag) toRemove.push_back(node);
        }
        for (auto n : toRemove) n->removeFromParent();
    }

    void purgeGhosts() {
        purgeGhostsFromParent(m_textLabel ? m_textLabel->getParent() : nullptr);
        purgeGhostsFromParent(m_textArea && m_textArea->m_label ? m_textArea->m_label->getParent() : nullptr);
    }

    Ghost ghostFrom(CCSprite* sprite) const {
        auto parent = sprite->getParent();
        return {
            sprite->getTexture(),
            sprite->getTextureRect(),
            parent ? parent->convertToWorldSpace(sprite->getPosition()) : sprite->convertToWorldSpace(CCPointZero),
            sprite->getAnchorPoint(),
            sprite->getScaleX(),
            sprite->getScaleY(),
            sprite->getRotation(),
            sprite->getColor(),
            sprite->getOpacity(),
        };
    }

    void captureLabelSnapshot(TextAreaGlyphs const* ta) {
        auto& prev = m_fields->prevLabel;
        prev.clear();
        if (!activeLabel()) return;

        auto const& text = m_fields->prevString;
        prev.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            if (auto sprite = glyphAt(text, i, ta)) prev.push_back(ghostFrom(sprite));
            else prev.push_back({});
        }
    }

    void forEachGlyph(auto const& fn) const {
        if (m_textArea && m_textArea->m_label) {
            auto chars = m_textArea->m_label->m_characters;
            if (!chars) return;
            for (unsigned int i = 0; i < chars->count(); ++i)
                fn(typeinfo_cast<CCSprite*>(chars->objectAtIndex(i)));
            return;
        }
        if (!m_textLabel) return;
        auto children = m_textLabel->getChildren();
        if (!children) return;
        for (unsigned int i = 0; i < children->count(); ++i)
            fn(typeinfo_cast<CCSprite*>(children->objectAtIndex(i)));
    }

    void finalizeActiveLabelFades() {
        GLubyte fullOp = activeLabelOpacity();
        forEachGlyph([&](CCSprite* s) {
            if (!s) return;
            s->stopActionByTag(kFadeInTag);
            s->setOpacity(fullOp);
        });
    }

    void plainRefresh() {
        CCTextInputNode::refreshLabel();
        finalizeActiveLabelFades();
        m_fields->prevString = m_textField->getString();
        m_fields->pending.clear();
        auto ta = textAreaGlyphs(m_fields->prevString);
        captureLabelSnapshot(&ta);
    }

    void runFadeIn(CCSprite* sprite, float dur, PopSettings const& pop, std::chrono::steady_clock::time_point now, size_t idx) {
        sprite->stopActionByTag(kFadeInTag);
        sprite->setOpacity(0);

        float angle, dist;
        resolvePop(pop, angle, dist);

        CCPoint motion(0, 0);
        CCAction* action;
        if (dist > 0.0f) {
            CCPoint finalPos = sprite->getPosition();
            motion = popMotion(angle, dist);
            sprite->setPosition(finalPos - motion);
            action = CCSpawn::create(CCFadeIn::create(dur), CCMoveTo::create(dur, finalPos), nullptr);
        } else {
            action = CCFadeIn::create(dur);
        }
        action->setTag(kFadeInTag);
        sprite->runAction(action);
        m_fields->pending[idx] = PendingFade{now, dur, motion};
    }

    void fadeInRange(size_t start, size_t end, AnimSettings const& cfg, TextAreaGlyphs const* ta) {
        if (start >= end || !activeLabel()) return;

        auto now = std::chrono::steady_clock::now();
        auto const& text = m_fields->prevString;
        for (size_t i = start; i < end; ++i) {
            if (auto sprite = glyphAt(text, i, ta)) runFadeIn(sprite, cfg.fadeIn, cfg.popIn, now, i);
        }
    }

    void continueFade(size_t idx, TextAreaGlyphs const* ta) {
        if (!activeLabel() || idx >= m_fields->pending.size()) return;
        auto& slot = m_fields->pending[idx];
        if (!slot) return;

        auto sprite = glyphAt(m_fields->prevString, idx, ta);
        if (!sprite) { slot = std::nullopt; return; }

        float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - slot->startTime).count();
        if (elapsed >= slot->duration) { slot = std::nullopt; return; }

        float remaining = slot->duration - elapsed;
        float ratio = elapsed / slot->duration;
        GLubyte fullOp = activeLabelOpacity();

        sprite->stopActionByTag(kFadeInTag);
        sprite->setOpacity(static_cast<GLubyte>(ratio * fullOp));

        CCAction* action;
        if (slot->motion.x != 0.0f || slot->motion.y != 0.0f) {
            CCPoint finalPos = sprite->getPosition();
            CCPoint offset = slot->motion * (1.0f - ratio);
            sprite->setPosition(finalPos - offset);
            action = CCSpawn::create(CCFadeTo::create(remaining, fullOp), CCMoveTo::create(remaining, finalPos), nullptr);
        } else {
            action = CCFadeTo::create(remaining, fullOp);
        }
        action->setTag(kFadeInTag);
        sprite->runAction(action);
    }

    void continueFadesExcept(size_t start, size_t end, TextAreaGlyphs const* ta) {
        for (size_t i = 0; i < m_fields->pending.size(); ++i) {
            if (i >= start && i < end) continue;
            if (m_fields->pending[i]) continueFade(i, ta);
        }
    }

    void fadeOutGhosts(std::vector<Ghost> const& ghosts, AnimSettings const& cfg) {
        auto label = activeLabel();
        if (ghosts.empty() || !label) return;
        auto parent = label->getParent();
        if (!parent) return;

        float lblScaleX = label->getScaleX();
        float lblScaleY = label->getScaleY();
        float lblRot    = label->getRotation();
        int   zOrder    = label->getZOrder();
        float dur = cfg.fadeOut;

        for (auto const& g : ghosts) {
            if (!g.tex) continue;
            auto s = CCSprite::createWithTexture(g.tex, g.rect);
            if (!s) continue;

            s->setPosition(parent->convertToNodeSpace(g.pos));
            s->setAnchorPoint(g.anchor);
            s->setScaleX(g.scaleX * lblScaleX);
            s->setScaleY(g.scaleY * lblScaleY);
            s->setRotation(g.rotation + lblRot);
            s->setColor(g.color);
            s->setOpacity(g.opacity);
            parent->addChild(s, zOrder);
            s->setTag(kGhostTag);

            float angle, dist;
            resolvePop(cfg.popOut, angle, dist);
            CCActionInterval* fade = (dist > 0.0f)
                ? static_cast<CCActionInterval*>(CCSpawn::create(
                    CCFadeOut::create(dur), CCMoveBy::create(dur, popMotion(angle, dist)), nullptr))
                : static_cast<CCActionInterval*>(CCFadeOut::create(dur));
            s->runAction(CCSequence::create(fade, CCRemoveSelf::create(), nullptr));
        }
    }

    void setString(gd::string text) {
        m_fields->externalUpdate = true;
        CCTextInputNode::setString(text);
        m_fields->externalUpdate = false;
    }

    void refreshLabel() {
        if (m_fields->externalUpdate) { skipAnimate(); return; }
        if (!m_selected) { plainRefresh(); return; }
        if (getParentByType<SettingNodeV3>(0)) { skipAnimate(); return; }

        auto cfg = loadConfig();
        if (!animationsActive(cfg)) { skipAnimate(); return; }

        std::string newStr = m_textField->getString();
        auto& oldStr = m_fields->prevString;
        auto ta = textAreaGlyphs(oldStr);

        if (oldStr == newStr) {
            CCTextInputNode::refreshLabel();
            continueFadesExcept(0, 0, &ta);
            captureLabelSnapshot(&ta);
            return;
        }

        auto [oIt, nIt] = std::mismatch(oldStr.begin(), oldStr.end(), newStr.begin(), newStr.end());
        size_t p = static_cast<size_t>(nIt - newStr.begin());

        auto [oRIt, nRIt] = std::mismatch(
            oldStr.rbegin(), oldStr.rend() - p, newStr.rbegin(), newStr.rend() - p);
        size_t s = static_cast<size_t>(nRIt - newStr.rbegin());

        size_t newStart = p, newEnd = newStr.size() - s;
        size_t oldStart = p, oldEnd = oldStr.size() - s;
        size_t newLen = newStr.size();

        std::vector<Ghost> ghosts;
        if (oldStart < oldEnd) {
            ghosts.reserve(oldEnd - oldStart);
            for (size_t i = oldStart; i < oldEnd && i < m_fields->prevLabel.size(); ++i)
                ghosts.push_back(m_fields->prevLabel[i]);
        }

        std::vector<std::optional<PendingFade>> remapped(newLen);
        auto& oldPending = m_fields->pending;
        for (size_t i = 0; i < newStart && i < oldPending.size(); ++i) remapped[i] = oldPending[i];
        for (size_t i = newEnd; i < newLen; ++i) {
            size_t oldIdx = (newEnd >= oldEnd) ? i - (newEnd - oldEnd) : i + (oldEnd - newEnd);
            if (oldIdx < oldPending.size()) remapped[i] = oldPending[oldIdx];
        }
        m_fields->pending = std::move(remapped);

        CCTextInputNode::refreshLabel();
        oldStr = std::move(newStr);
        ta = textAreaGlyphs(oldStr);

        continueFadesExcept(newStart, newEnd, &ta);
        fadeInRange(newStart, newEnd, cfg, &ta);
        fadeOutGhosts(ghosts, cfg);
        captureLabelSnapshot(&ta);
    }
};
