# WeBlog 项目结构说明

## ? 项目文件组织

```
weblog/
├── index.html          # 主HTML文件（页面结构）
├── styles.css          # 样式表（所有CSS样式）
├── script.js           # 脚本文件（所有JavaScript功能）
└── weblog.html         # 原始单文件版本（已废弃）
```

## ? 文件说明

### `index.html`
- **用途**：页面结构和内容
- **内容**：HTML标签、语义化标记
- **引入**：`<link rel="stylesheet" href="./styles.css">` 和 `<script src="./script.js"></script>`
- **特点**：
  - 清晰的语义结构
  - 5个主要章节（个人介绍、课程过程、课程总结、中期项目、期末项目）
  - 响应式设计支持
  - 媒体资源使用相对路径

### `styles.css`
- **用途**：所有样式定义
- **内容**：
  - CSS 变量定义（颜色、尺寸等）
  - 动画关键帧（fadeInUp、slideInLeft等）
  - 响应式媒体查询
  - 现代化视觉效果（渐变、阴影、动画）
- **特点**：
  - 使用 CSS 自定义属性便于主题切换
  - 完整的动画系统
  - 移动端优先设计
  - 悬停交互反馈

### `script.js`
- **用途**：所有交互功能
- **功能**：
  1. **返回顶部**：滚动300px后显示按钮，平滑滚回顶部
  2. **页面加载动画**：卡片依序淡入
  3. **平滑滚动**：导航链接平滑跳转到对应章节
- **特点**：
  - 模块化函数设计
  - 使用事件委托
  - DOMContentLoaded 事件确保DOM加载完成

## ? 使用方法

### 本地查看
1. 在浏览器中打开 `index.html` 即可
2. 或使用本地服务器：
   ```bash
   python -m http.server 8000
   # 访问 http://localhost:8000/weblog/
   ```

### 部署到服务器
将 `index.html`、`styles.css`、`script.js` 三个文件上传到服务器即可

### 修改内容
- **修改文本/链接**：编辑 `index.html`
- **修改样式/主题**：编辑 `styles.css` 中的 `:root` 变量或具体样式
- **修改交互**：编辑 `script.js` 中的函数

## ? 主要特性

### 视觉效果
- 深色主题（Dark Mode）
- 渐变背景（固定）
- 毛玻璃效果（Glassmorphism）
- 动画过渡（Smooth Transitions）

### 交互功能
- ? 页面加载动画
- ? 平滑滚动导航
- ?? 返回顶部按钮
- ? 悬停效果

### 响应式设计
- 移动端（< 860px）：单列布局
- 桌面端（≥ 860px）：多列布局
- 图库自适应（3列→单列）

## ? 自定义

### 修改颜色主题
编辑 `styles.css` 的 `:root` 部分：
```css
:root {
  --bg: #0b0c10;           /* 背景色 */
  --card: #14161d;         /* 卡片背景 */
  --text: #e9eef7;         /* 文字色 */
  --muted: #aeb8c7;        /* 辅助文字 */
  --accent1: #7aa7ff;      /* 主色调 */
  --accent3: #00d4ff;      /* 辅色调 */
}
```

### 修改动画速度
在 `styles.css` 中调整 `transition` 和 `animation` 的时间值

### 添加新功能
在 `script.js` 中添加新函数，并在 `DOMContentLoaded` 中调用

## ? 文件大小参考
- `index.html`：~12 KB（内容完整）
- `styles.css`：~6 KB（所有样式）
- `script.js`：~1.5 KB（交互脚本）
- **总计**：< 20 KB（高效加载）

## ? 浏览器兼容性
- Chrome 90+
- Firefox 88+
- Safari 14+
- Edge 90+

---

**最后修改**：2026-05-30  
**项目**：从代码到实物：造你所想 - 创客学习展示页
