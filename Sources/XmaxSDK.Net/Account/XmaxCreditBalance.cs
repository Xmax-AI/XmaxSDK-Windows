//
//  XmaxCreditBalance.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>The server-reported credit balance for the account owning the client's API key.</summary>
/// <param name="CurrentBalance">Finite, nonnegative credits, including fractional credits.</param>
public sealed record XmaxCreditBalance(double CurrentBalance);
