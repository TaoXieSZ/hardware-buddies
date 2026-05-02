import Foundation

public extension VoiceSubAgent {
    /// 创建一个飞书发消息专家 subagent。
    /// 通过 lark-cli（内置于 app bundle）以用户身份发送消息，无需 App 自身存储飞书凭证。
    ///
    /// - Parameters:
    ///   - contacts: 预指定的联系人列表，subagent 可以按名字发消息
    ///   - llmClient: LLM 客户端（用于 subagent 自身的推理）
    ///   - model: LLM 模型名
    ///   - eventHandler: 可选的事件回调
    /// - Returns: 配置好的 VoiceSubAgent（lark-cli 内置时始终可用）
    static func feishuMessenger(
        contacts: [FeishuContact] = [],
        llmClient: (any VoiceAgentLLMClient)? = nil,
        model: String = VoiceAgentRuntimeConfig.openAIModel,
        eventHandler: VoiceSubAgentEventHandler? = nil
    ) -> VoiceSubAgent {
        let resolvedContacts = contacts.isEmpty ? FeishuContact.configuredContacts() : contacts
        let contactList = resolvedContacts.isEmpty
            ? "No local contacts pre-configured. User can provide a direct Feishu ID (open_id, chat_id) to send messages."
            : resolvedContacts.map { contact in
                let aliases = contact.aliases.isEmpty ? "" : " aliases: \(contact.aliases.joined(separator: ", "))"
                return "- \(contact.name) (\(contact.idType.rawValue): \(contact.id))\(aliases)"
            }.joined(separator: "\n")

        let systemPrompt = """
        # 身份

        你是飞书发消息专家，负责通过 lark-cli 以用户身份发送文字消息。

        # 能力

        1. **发送消息**：向指定联系人或群组发送文字消息。可以用预指定联系人的名字，也可以用飞书 ID 直接发。
        2. **查询联系人**：查本地预配置联系人/别名。

        # 预配置联系人

        \(contactList)

        # 执行原则

        ## 意图判断

        - 只有当上级明确要求你"发送飞书消息"时，才可以调用 feishu_send_message。
        - 如果任务内容只是陈述计划，例如"我等会要给 X 发 Y""我准备去飞书找 X"，不要发送消息；回复需要用户确认是否代发。
        - 如果表达模糊，例如"我要给 X 发 Y"，先确认"要我现在替你通过飞书发送吗？"，不要直接发送。
        - 如果缺少收件人或消息内容，先追问缺失信息。
        - 如果查到多个候选联系人，先让用户选择，不能任选一个发送。

        ## 发送流程

        - 发消息前确认收件人和内容，避免发错。
        - 如果用户给了联系人名字，用 feishu_lookup_contact 确认。
        - 如果用户没有给出收件人 ID 或联系人名，先要求用户补充，不要猜测。
        - 如果找到多个同名/近似联系人，先让用户选择，不要直接发送。
        - 返回结果时简明扼要：「已发送给 XXX」。
        - 如果遇到错误（如 lark-cli 未登录），说明原因并建议用户在设置中完成飞书登录。
        """

        let tools: [any VoiceAgentTool] = [
            FeishuSendMessageTool(contacts: resolvedContacts),
            FeishuLookupContactTool(contacts: resolvedContacts),
        ]

        return VoiceSubAgent(
            name: "feishu",
            purpose: "Send text messages via Feishu/Lark. Has pre-configured contacts for quick messaging.",
            model: model,
            systemPrompt: systemPrompt,
            client: llmClient ?? OpenAICompatibleChatClient.configuredOpenAI(),
            tools: tools,
            eventHandler: eventHandler
        )
    }
}
