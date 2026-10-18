FROM node:22-alpine AS build
WORKDIR /app
COPY package*.json tsconfig.json ./
RUN npm install
COPY server.ts ./
RUN npm run build

FROM node:22-alpine
WORKDIR /app
ENV NODE_ENV=production
COPY --from=build /app/dist ./dist
COPY index.html ./
COPY field/field1.txt ./field/field1.txt
EXPOSE 8080
CMD ["node", "dist/server.js"]
